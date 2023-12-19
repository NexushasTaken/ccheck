#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <ftw.h>
#include "logger.h"

typedef struct {
  char **elems;
  size_t count;
  size_t capacity;
} Cstr_array;

typedef struct {
  struct timespec binary_mtime;
  struct timespec most_recent_mtime;

  int level_deep; // used for recursive function
  int indent_width;

  Cstr_array invalid_files;
  long NAME_LEN_MAX;
  long PATH_LEN_MAX;
  char *ORIG_CWD;

  FILE *_cache_stream; // only used for get_cache_stream() and close_cache_stream()
} Context;

static Context ctx;

#define MALLOC(ptr, size)                          \
  do {                                             \
    ptr = malloc(size);                            \
    ASSERT_NULL(ptr, "could not allocate memory"); \
  } while (0)

#define is_dir(path) (S_ISDIR(get_file_mode(path)))
#define is_reg(path) (S_ISREG(get_file_mode(path)))
mode_t get_file_mode(const char *filepath) {
  struct stat buf;

  ASSERT_ERR(stat(filepath, &buf), "could not stat %s", filepath);
  return buf.st_mode;
}

struct timespec get_file_mtime(const char *filepath) {
  struct stat buf;

  ASSERT_ERR(stat(filepath, &buf), "could not stat %s", filepath);
  return buf.st_mtim;
}

void mkdir_if_not_exist(const char *path) {
  int ret = mkdir(path, 0755);
  if (ret < 0) {
    if (errno == EEXIST) {
      if (S_ISDIR(get_file_mode(path))) {
        return;
      } else {
        PANIC("%s exist but it's not a directory", path);
      }
    }
    PANIC("could not create %s directory: %s", path, strerror(errno));
  }
}

FILE *get_cache_stream() {
#define CACHE_FILE ".cache/ccheck.db"
  if (ctx._cache_stream == NULL) {
    mkdir_if_not_exist(".cache/");
    int fd = open(CACHE_FILE, O_RDWR|O_CREAT, 0644);
    if (fd < 0) {
      if (errno == EEXIST) {
        fd = open(CACHE_FILE, O_RDWR);
        ASSERT_ERR(fd, "could not open %s", CACHE_FILE);
      } else {
        ASSERT_ERR(fd, "could not create %s", CACHE_FILE);
      }
    }

    ctx._cache_stream = fdopen(fd, "r+");
    ASSERT_NULL(ctx._cache_stream, "could not create a stream for file descriptor %d", fd);
  }
  return ctx._cache_stream;
}

void close_cache_stream() {
  int fd, i;

  if (ctx._cache_stream == NULL && ctx.invalid_files.count == 0) {
    return;
  }

  if (ctx.invalid_files.count > 0) {
    get_cache_stream();
  }

  fd = fileno(ctx._cache_stream);
  ASSERT_ERR(fd, "could not get the file descriptor from stream");
  ASSERT_ERR(ftruncate(fd, 0), "could not truncate the contents of file descriptor %d", fd);
  rewind(ctx._cache_stream);

  if (ctx.invalid_files.count > 0) {
    for (i = 0; i < ctx.invalid_files.count; i += 1) {
      fprintf(ctx._cache_stream, "%s\n", ctx.invalid_files.elems[i]);
    }
    fflush(ctx._cache_stream);
  }

  fclose(ctx._cache_stream);
}

int cstr_array_contains(const Cstr_array *arr, const char *str) {
  for (int i = 0; i < ctx.invalid_files.count; i += 1) {
    if (strncmp(str, ctx.invalid_files.elems[i], strlen(str)) == 0) {
      return 1;
    }
  }
  return 0;
}

void cstr_array_realloc(Cstr_array *arr, size_t new_size) {
  if (arr->capacity >= new_size) {
    PANIC("capacity %ld must be larger than %ld", arr->capacity, new_size);
  }
  char **new_arr;
  MALLOC(new_arr, sizeof(char*) * new_size);
  if (arr->count > 0) {
    new_arr = memcpy(new_arr, arr->elems, new_size);
  }
  free(arr->elems);
  arr->elems = new_arr;
  arr->capacity = new_size;
}

void cstr_array_remove(Cstr_array *arr, size_t index) {
  if (index >= arr->count) {
    PANIC("%ld is index out of bounds", index);
  }
  free(arr->elems[index]);
  index += 1;
  while (index < arr->count) {
    arr->elems[index-1] = arr->elems[index];
    index += 1;
  }
  arr->count -= 1;
}

void cstr_array_free_data(Cstr_array *arr) {
  if (arr->elems == NULL) {
    return;
  }
  for (int i = 0; i < arr->count; i += 1) {
    free(arr->elems[i]);
  }
  free(arr->elems);
}

void cstr_array_append(Cstr_array *arr, const char *const str) {
  if (arr->count >= arr->capacity) {
    cstr_array_realloc(arr, arr->capacity > 0 ? arr->capacity * 2 : 8);
  }
  char *dup = strdup(str);
  ASSERT_NULL(dup, "could not duplicate string %s", str);
  arr->elems[arr->count] = dup;
  arr->count += 1;
}

int cstr_ends_with(const char *str, const char *postfix) {
  const size_t cstr_len = strlen(str);
  const size_t postfix_len = strlen(postfix);
  return postfix_len <= cstr_len
    && strcmp(str + cstr_len - postfix_len, postfix) == 0;
}

int is_file_dir_exist(const char *filepath) {
  struct stat buf;
  int ret;

  ret = stat(filepath, &buf);
  if (ret < 0) {
    if (errno == ENOENT) {
      return 0;
    }
    ASSERT_ERR(ret, "could not stat %s: %s", filepath, strerror(errno));
  }
  return 1;
}

#define AINFO_INDENT(end, ...) AINFO(ctx.level_deep * ctx.indent_width, end, __VA_ARGS__)

long path_conf(const int name) {
  long value = pathconf("/", name);
  ASSERT_ERR(value, "could not get pathconf value for %d", name);
  return value;
}

// return status code
int run_analyzer(const char *filepath) {
  return 1;
}

void check_src_file(const char *filename) {
  int success = run_analyzer(filename) == 0;
  if (!success) {
    cstr_array_append(&ctx.invalid_files, filename);
  }
  printf("%s - %s\n", filename, success ? "done" : "error");
  if (!success) {
    cstr_array_append(&ctx.invalid_files, filename);
  }
}

void check_src_syntax(const char *filepath) {
  struct timespec sec;

  if (cstr_ends_with(filepath, ".c") ||
      cstr_ends_with(filepath, ".h")) {
    AINFO_INDENT("", "%s - ", filepath);
    sec = get_file_mtime(filepath);
    if (sec.tv_sec > ctx.binary_mtime.tv_sec) {
      if (run_analyzer(filepath) > 0) {
        fprintf(stderr, "error");
        cstr_array_append(&ctx.invalid_files, filepath);
      }
    } else {
      fprintf(stderr, "done");
    }
    if (sec.tv_sec > ctx.most_recent_mtime.tv_sec) {
      ctx.most_recent_mtime = sec;
    }
    fprintf(stderr, "\n");
  }
}

void init(int argc, char **argv) {
  assert(argc > 0);
  ctx.binary_mtime = get_file_mtime(argv[0]);
  ctx.most_recent_mtime = ctx.binary_mtime;
  ctx.indent_width = 2;

  ctx.NAME_LEN_MAX = path_conf(_PC_NAME_MAX);
  ctx.PATH_LEN_MAX = path_conf(_PC_PATH_MAX);

  ctx.invalid_files = (Cstr_array){0};
  MALLOC(ctx.ORIG_CWD, ctx.PATH_LEN_MAX+1);
  ASSERT_NULL(getcwd(ctx.ORIG_CWD, ctx.PATH_LEN_MAX), "could not get current working directory");

#ifndef CCHECK_TEST
  if (is_file_dir_exist(CACHE_FILE)) {
    FILE *stream = get_cache_stream();
    char *line;

    MALLOC(line, ctx.PATH_LEN_MAX+1);
    *line = 0;
    errno = 0; // does errors even occured when fgets are used?
    while (fgets(line, ctx.PATH_LEN_MAX+1, stream) != NULL) {
      size_t linesz = strnlen(line, ctx.PATH_LEN_MAX);
      if (line[linesz - 1] == '\n') {
        line[linesz - 1] = '\0';
      }
      if (is_file_dir_exist(line)) {
        check_src_file(line);
      }
      *line = 0;
    }
  }
#endif
}

void cleanup() {
  close_cache_stream();
  cstr_array_free_data(&ctx.invalid_files);
}

void set_file_mtime(const char *filepath, const struct timespec mtime) {
  struct timespec ts[2];
  struct stat statbuf;
  int file_fd;

  ASSERT_ERR((file_fd = open(filepath, O_RDONLY)), "could not open %s", filepath);
  ASSERT_ERR(fstat(file_fd, &statbuf), "could not stat %s", filepath);

  ts[0] = statbuf.st_atim;
  ts[1] = mtime;

  ASSERT_ERR(futimens(file_fd, ts), "could not change the timestamp of %s", filepath);
  ASSERT_ERR(close(file_fd), "could not file descriptor %d", file_fd);
}

int process_entry(
    const char *fpath,
    const struct stat *sb,
    int typeflag,
    struct FTW *ftwbuf) {
  if (typeflag != FTW_F || cstr_array_contains(&ctx.invalid_files, fpath)) {
    return 0;
  }
  ctx.level_deep = ftwbuf->level;

  if (cstr_ends_with(fpath, ".c") ||
      cstr_ends_with(fpath, ".h")) {
    int success = 1;
    if (sb->st_mtime > ctx.binary_mtime.tv_sec) {
      success = run_analyzer(fpath) == 0;
    }
    if (sb->st_mtime > ctx.most_recent_mtime.tv_sec) {
      ctx.most_recent_mtime = sb->st_mtim;
    }

    printf("%s - %s\n", fpath, success ? "done" : "error");
    if (!success) {
      cstr_array_append(&ctx.invalid_files, fpath);
    }
  }
  return 0;
}

int walk_tree(const char *dirpath) {
  ASSERT_ERR(nftw(dirpath, process_entry, -1, 0), "could not traverse %s directory", dirpath);
  return 0;
}

#ifdef CCHECK_TEST
int ccheck_main(int argc, char **argv) {
#else
int main(int argc, char **argv) {
#endif
  init(argc, argv);

  const char *target = ".";
  if (argc == 2) {
    target = argv[1];
  }

  if (S_ISDIR(get_file_mode(target))) {
    walk_tree(target);
  } else {
    PANIC("%s is not a directory", target);
  }

  set_file_mtime(argv[0], ctx.most_recent_mtime);
  cleanup();
  return 0;
}
