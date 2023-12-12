#define _POSIX_C_SOURCE 200809L
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
#include "logger.h"

typedef char* Cstr;

typedef struct {
  Cstr *elems;
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
  Cstr ORIG_CWD;
} Context;

static Context ctx;

Cstr get_cwd_with_filename(Cstr filename) {
  filename = strndup(filename, ctx.NAME_LEN_MAX);
  ASSERT_NULL(filename, "could not duplicate string \"%s\"", filename);

  size_t buffer_size = strlen(ctx.ORIG_CWD) + strlen(filename) + 1;
  Cstr buffer = malloc(buffer_size);
  ASSERT_NULL(buffer, "could not allocate memory");

  stpcpy(stpcpy(buffer, ctx.ORIG_CWD), filename);

  free(filename);
  return buffer;
}

int cstr_array_indexof_cstr(const Cstr_array *arr, const Cstr str) {
  for (int i = 0; i < ctx.invalid_files.count; i += 1) {
    if (strncmp(str, ctx.invalid_files.elems[i], strlen(str)) == 0) {
      return i;
    }
  }
  return -1;
}

void cstr_array_realloc(Cstr_array *arr, size_t new_size) {
  if (arr->capacity >= new_size) {
    PANIC("capacity %ld must be larger than %ld", arr->capacity, new_size);
  }
  Cstr *new_arr = malloc(sizeof(Cstr) * new_size);
  ASSERT_NULL(new_arr, "cannot allocate memory");
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

void cstr_array_append(Cstr_array *arr, const Cstr str) {
  if (arr->count >= arr->capacity) {
    cstr_array_realloc(arr, arr->capacity ? arr->capacity * 2 : 8);
  }
  arr->elems[arr->count] = strdup(str);
  arr->count += 1;
}

int cstr_ends_with(const Cstr str, const Cstr postfix) {
  const size_t cstr_len = strlen(str);
  const size_t postfix_len = strlen(postfix);
  return postfix_len <= cstr_len
    && strcmp(str + cstr_len - postfix_len, postfix) == 0;
}

struct timespec get_file_mtime(const Cstr filepath) {
  struct stat statbuf;

  ASSERT_ERR(stat(filepath, &statbuf), "could not stat %s", filepath);
  return statbuf.st_mtim;
}

mode_t get_file_mode(const Cstr filepath) {
  struct stat statbuf;

  ASSERT_ERR(stat(filepath, &statbuf), "could not stat %s", filepath);
  return statbuf.st_mode;
}

#define AINFO_INDENT(end, ...) AINFO(ctx.level_deep * ctx.indent_width, end, __VA_ARGS__)

long path_conf(const int name) {
  long value = pathconf("/", name);
  ASSERT_ERR(value, "could not get pathconf value for %d", name);
  return value;
}

void init(int argc, char **argv) {
  assert(argc > 0);
  ctx.binary_mtime = get_file_mtime(argv[0]);
  ctx.most_recent_mtime = ctx.binary_mtime;
  ctx.indent_width = 2;

  ctx.NAME_LEN_MAX = path_conf(_PC_NAME_MAX);
  ctx.PATH_LEN_MAX = path_conf(_PC_PATH_MAX);

  ctx.invalid_files = (Cstr_array){0};
  ctx.ORIG_CWD = malloc(ctx.PATH_LEN_MAX+1);
  ASSERT_NULL(ctx.ORIG_CWD, "could not allocate memory");
  ASSERT_NULL(getcwd(ctx.ORIG_CWD, ctx.PATH_LEN_MAX), "could not get current working directory");
}

void set_file_mtime(const Cstr filepath, const struct timespec mtime) {
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

int run_analyzer(const Cstr filepath) {
  printf("- done");
  return 0;
}

void check_src_syntax(const Cstr filepath) {
  struct timespec sec;

  if (cstr_ends_with(filepath, ".c") ||
      cstr_ends_with(filepath, ".h")) {
    AINFO_INDENT(" ", "%s", filepath);
    sec = get_file_mtime(filepath);
    if (sec.tv_sec > ctx.binary_mtime.tv_sec) {
      if (run_analyzer(filepath)) {
        // cstr_array_append(&ctx.invalid_files, get_cwd_with_filename(filepath));
      }
    }
    if (sec.tv_sec > ctx.most_recent_mtime.tv_sec) {
      ctx.most_recent_mtime = sec;
    }
    printf("\n");
  }
}

#define CHDIR(dir)                                 \
  do {                                             \
    if (chdir(dir) != 0) {                         \
      PANIC("could not change current directory"); \
    }                                              \
    AINFO_INDENT("\n", "=checking %s/", dir);      \
  } while (0) 

void traverse_directory(const Cstr dirpath) {
  struct dirent *entry_buf;
  mode_t file_mode;
  DIR *parent;

  errno = 0;
  parent = opendir(dirpath);  
  ASSERT_NULL(parent, "could not open directory %s", dirpath);

  // AINFO_INDENT("checking %s", dirpath);
  CHDIR(dirpath);
  ctx.level_deep += 1;

  while ((entry_buf = readdir(parent)) != NULL) {
    if (strcmp(entry_buf->d_name, ".") == 0 ||
        strcmp(entry_buf->d_name, "..") == 0) {
      continue;
    }
    file_mode = get_file_mode(entry_buf->d_name);
    if (S_ISDIR(file_mode)) {
      traverse_directory(entry_buf->d_name);
    } else if (S_ISREG(file_mode)) {
      check_src_syntax(entry_buf->d_name);
    }
  }

  if (errno != 0) {
    PANIC("could not read directory");
  }
  closedir(parent);
  ctx.level_deep -= 1;
  CHDIR("..");
}

#ifdef CCHECK_TEST
int ccheck_main(int argc, char **argv) {
#else
int main(int argc, char **argv) {
#endif
  if (argc != 2) {
    PANIC("usage: %s <filename/directory>", argv[0]);
  }

  init(argc, argv);

  Cstr target = ".";
  target = argv[1];
  if (S_ISDIR(get_file_mode(target))) {
    traverse_directory(target);
  } else {
    PANIC("%s is not a directory", target);
  }

  set_file_mtime(argv[0], ctx.most_recent_mtime);
  return 0;
}
