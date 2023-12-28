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
#include <getopt.h>
#include "logger.h"

typedef struct {
  char **elems;
  size_t count;
  size_t capacity;
} Cstr_array;

typedef struct {
  struct timespec binary_mtime;
  struct timespec most_recent_mtime;

  int tab_width;
  int verbose;

  Cstr_array valid_files;
  Cstr_array invalid_files;
  long NAME_LEN_MAX;
  long PATH_LEN_MAX;
  char *ORIG_CWD;

  char **target_dirs;
  int target_dirs_length;

  // used by walk_tree()
  char *current_dir;
  size_t current_dir_len;

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
  int ret;

  ret = mkdir(path, 0755);
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
  int fd;

  if (ctx._cache_stream == NULL) {
    mkdir_if_not_exist(".cache/");
    fd = open(CACHE_FILE, O_RDWR|O_CREAT, 0644);
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

char* cstr_array_pop(Cstr_array *arr) {
  if (arr->count == 0) {
    PANIC("could not pop: array is empty");
  }
  arr->count -= 1;
  return arr->elems[arr->count];
}

int cstr_array_contains(const Cstr_array *arr, const char *str) {
  for (int i = 0; i < arr->count; i += 1) {
    if (strncmp(str, arr->elems[i], strlen(str)) == 0) {
      return 1;
    }
  }
  return 0;
}

void cstr_array_realloc(Cstr_array *arr, size_t new_size) {
  char **new_arr;

  if (arr->capacity >= new_size) {
    PANIC("capacity %ld must be larger than %ld", arr->capacity, new_size);
  }
  MALLOC(new_arr, sizeof(char*) * new_size);
  new_arr = memcpy(new_arr, arr->elems, sizeof(char*) * arr->count);
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
  int i;

  if (arr->elems == NULL) {
    return;
  }
  for (i = 0; i < arr->count; i += 1) {
    free(arr->elems[i]);
  }
  free(arr->elems);
  memset(arr, 0, sizeof(Cstr_array));
}

void cstr_array_append(Cstr_array *arr, const char *const str) {
  char *dup;

  if (arr->count >= arr->capacity) {
    cstr_array_realloc(arr, arr->capacity > 0 ? arr->capacity * 2 : 8);
  }
  dup = strdup(str);
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

#define path_conf(var, name)                                      \
  do {                                                            \
    var = pathconf("/", name);                                    \
    ASSERT_ERR(var, "could not get pathconf value for %d", name); \
  } while (1);

#define sys_conf(var, name)                                      \
  do {                                                           \
    var = sysconf(name);                                         \
    ASSERT_ERR(var, "could not get sysconf value for %d", name); \
  } while (1);

// return status code
int run_analyzer(const char *filepath) {
  return 1;
}

int is_newer(const struct timespec target, const struct timespec file) {
  return file.tv_sec > target.tv_sec;
}

// force check fpath if sb is NULL
void check_src_file(const char *fpath, const struct stat *sb) {
  int success;

  success = 1;
  if (sb == NULL || is_newer(ctx.binary_mtime, sb->st_mtim)) {
    success = run_analyzer(fpath) == 0;
  }
  printf("%s - %s\n",
      fpath + (ctx.current_dir_len ? ctx.current_dir_len + 1 : 0),
      success ? "done" : "error");
  if (success) {
    cstr_array_append(&ctx.valid_files, fpath);
  } else {
    cstr_array_append(&ctx.invalid_files, fpath);
  }
}

static const struct option long_options[] = {
  {"tabwidth", required_argument, 0, 'w'},
  {"verbose",  no_argument      , 0, 'v'},
  {0, 0, 0, 0}
};

long str_to_long(const char *str) {
  char *last;
  long value;

  value = strtol(str, &last, 10);
  errno = 0;
  if (*str != '\0' && *last == '\0') {
    return value;
  } else {
    fprintf(stderr, "the %s value is not a valid number\n", str);
    exit(EXIT_FAILURE);
  }
  return 0;
}

void parse_arguments(int argc, char **argv) {
  static char *default_target_dirs[] = {"."};
  int state;
  int option_index;

  state = 0;
  option_index = 0;
  while (state != -1) {
    state = getopt_long(argc, argv, "w:v", long_options, &option_index);
    switch (state) {
      case 'w':
        ctx.tab_width = str_to_long(optarg);
        break;
      case 'v':
        ctx.verbose = 1;
        break;
      case '?':
        exit(EXIT_FAILURE);
        break;
    }
  }

  if (optind == argc) {
    // TODO: think of another way to approach this
    ctx.target_dirs = default_target_dirs;
    ctx.target_dirs_length = 1;
  } else {
    // TODO: do some research if modifying the argv is fine
    ctx.target_dirs_length = argc - optind;
    ctx.target_dirs = argv + optind;
  }
}

void init(int argc, char **argv) {
  FILE *stream;
  char *line, *resolved_path;
  int i;
  size_t linesz;

  assert(argc > 0);
  memset(&ctx, 0, sizeof(Context)); // just to be sure that ctx data is set to zeros

  ctx.binary_mtime = get_file_mtime(argv[0]);
  ctx.most_recent_mtime = ctx.binary_mtime;
  ctx.tab_width = 2;

  path_conf(ctx.NAME_LEN_MAX, _PC_NAME_MAX);
  path_conf(ctx.PATH_LEN_MAX, _PC_PATH_MAX);

  parse_arguments(argc, argv);

  // change target_dirs elements to absolute path
  for (i = 0; i < ctx.target_dirs_length; i += 1) {
    MALLOC(resolved_path, ctx.PATH_LEN_MAX + 1);

    if (realpath(ctx.target_dirs[i], resolved_path) == NULL) {
      if (errno == ENOENT || errno == ENOTDIR) {
        fprintf(stderr, "\"%s\": %s\n", ctx.target_dirs[i], strerror(ENOENT));
        exit(EXIT_SUCCESS);
      }
      ASSERT_NULL(NULL, "error: %s", strerror(ENOENT));
    }

    ctx.target_dirs[i] = resolved_path;
  }

  // remove ORIG_CWD if wasn't tend to be used
  MALLOC(ctx.ORIG_CWD, ctx.PATH_LEN_MAX+1);
  ASSERT_NULL(getcwd(ctx.ORIG_CWD, ctx.PATH_LEN_MAX), "could not get current working directory");

#ifndef CCHECK_TEST
  if (is_file_dir_exist(CACHE_FILE)) {
    stream = get_cache_stream();

    MALLOC(line, ctx.PATH_LEN_MAX+1);
    *line = 0;
    errno = 0; // does errors even occured when fgets are used?
    while (fgets(line, ctx.PATH_LEN_MAX+1, stream) != NULL) {
      linesz = strnlen(line, ctx.PATH_LEN_MAX);
      if (line[linesz - 1] == '\n') {
        line[linesz - 1] = '\0';
      }
      if (is_file_dir_exist(line)) {
        check_src_file(line, NULL);
      }
      *line = 0;
    }
    free(line);
  }
#endif
}

void cleanup() {
  int i;
  close_cache_stream();
  cstr_array_free_data(&ctx.invalid_files);
  cstr_array_free_data(&ctx.valid_files);
  for (i = 0; i < ctx.target_dirs_length; i += 1) {
    free(ctx.target_dirs[i]);
  }
  free(ctx.ORIG_CWD);
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
  if (typeflag != FTW_F) {
    return 0;
  }

  if (cstr_ends_with(fpath + ftwbuf->base, ".c") ||
      cstr_ends_with(fpath + ftwbuf->base, ".h")) {
    if (cstr_array_contains(&ctx.invalid_files, fpath) ||
      cstr_array_contains(&ctx.valid_files,   fpath)) {
      return 0;
    }
    check_src_file(fpath, sb);
    if (is_newer(ctx.most_recent_mtime, sb->st_mtim)) {
      ctx.most_recent_mtime = sb->st_mtim;
    }
  }
  return 0;
}

int walk_tree(char *dirpath) {
  ctx.current_dir = dirpath;
  ctx.current_dir_len = strnlen(dirpath, ctx.PATH_LEN_MAX);

  printf("%s:\n", dirpath);
  ASSERT_ERR(nftw(dirpath, process_entry, -1, 0), "could not traverse %s directory", dirpath);
  return 0;
}

#ifdef CCHECK_TEST
int ccheck_main(int argc, char **argv) {
#else
int main(int argc, char **argv) {
#endif
  int i;

  init(argc, argv);

  for (i = 0; i < ctx.target_dirs_length; i += 1) {
    walk_tree(ctx.target_dirs[i]);
  }

  set_file_mtime(argv[0], ctx.most_recent_mtime);
  cleanup();
  return 0;
}
