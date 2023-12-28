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
#include <error.h>

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

  // becarefull using cstr_array_*() function in this member
  Cstr_array target_dirs;

  // used by walk_tree()
  char *current_dir;
  size_t current_dir_len;

  FILE *_cache_stream; // only used for get_cache_stream() and close_cache_stream()
} Context;

static Context ctx;

#define error_line(status, errnum, ...) \
  error_at_line(status, errnum, __FILE__, __LINE__, __VA_ARGS__)
#define ERROR_NO(condition, ...)               \
  do {                                         \
    if ((condition)) {                         \
      error(EXIT_FAILURE, errno, __VA_ARGS__); \
    }                                          \
  } while (0)

#define ERROR_NO_LINE(condition, ...)               \
  do {                                              \
    if ((condition)) {                              \
      error_line(EXIT_FAILURE, errno, __VA_ARGS__); \
    }                                               \
  } while (0)

#define path_conf(var, name)                      \
  do {                                            \
    var = pathconf("/", name);                    \
    ERROR_NO((var) == -1, "pathconf(%s)", #name); \
  } while (0);

#define sys_conf(var, name)                      \
  do {                                           \
    var = sysconf(name);                         \
    ERROR_NO((var) == -1, "sysconf(%s)", #name); \
  } while (0);

#define is_dir(path) (S_ISDIR(get_file_mode(path)))
#define is_reg(path) (S_ISREG(get_file_mode(path)))
mode_t get_file_mode(const char *filepath) {
  struct stat buf;

  ERROR_NO(stat(filepath, &buf) == -1, "stat(%s)", filepath);
  return buf.st_mode;
}

struct timespec get_file_mtime(const char *filepath) {
  struct stat buf;

  ERROR_NO(stat(filepath, &buf) == -1, "stat(%s)", filepath);
  return buf.st_mtim;
}

void mkdir_if_not_exist(const char *path) {
  int err;

  err = mkdir(path, 0755);
  if (err < 0) {
    if (errno == EEXIST && is_dir(path)) {
      return;
    }
    error(EXIT_FAILURE, errno, "cannot create directory '%s'", path);
  }
}

void *cmalloc(size_t sz) {
  void *p = malloc(sz);
  ERROR_NO(p == NULL, "malloc(%zu)", sz);
  return p;
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
        ERROR_NO(fd == -1, "could not open %s", CACHE_FILE);
      } else {
        ERROR_NO(EXIT_FAILURE, "could not create %s", CACHE_FILE);
      }
    }

    ctx._cache_stream = fdopen(fd, "r+");
    ERROR_NO(ctx._cache_stream == NULL, "fdopen(%s)", CACHE_FILE);
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

  ERROR_NO((fd = fileno(ctx._cache_stream)) == -1, "fileno");
  ERROR_NO(ftruncate(fd, 0) == -1, "ftruncate");
  rewind(ctx._cache_stream);

  if (ctx.invalid_files.count > 0) {
    for (i = 0; i < ctx.invalid_files.count; i += 1) {
      fprintf(ctx._cache_stream, "%s\n", ctx.invalid_files.elems[i]);
    }
    ERROR_NO_LINE(fflush(ctx._cache_stream) == EOF, "fflush");
  }

  ERROR_NO_LINE(fclose(ctx._cache_stream) == EOF, "fclose");
}

#define cstr_array_loop(arr, name, body) \
  for (int name##_i = 0; name##_i < (arr)->count; name##_i++) {\
    char *name = *((arr)->elems + name##_i);  (void)name;\
    char **name##_p = (arr)->elems + name##_i;(void)name##_p;\
    body\
  }

void cstr_array_from(Cstr_array *arr, char **data, size_t len) {
  arr->elems = data;
  arr->count = len;
  arr->capacity = len;
}

static int
cmpstringp(const void *p1, const void *p2) {
  return strcmp(*(const char **) p1, *(const char **) p2);
}

void cstr_array_sort(Cstr_array *arr) {
  qsort(arr->elems, arr->count, sizeof(char*), cmpstringp);
}

char* cstr_array_pop(Cstr_array *arr) {
  assert(arr->count > 0);
  arr->count -= 1;
  return arr->elems[arr->count];
}

int cstr_array_contains(const Cstr_array *arr, const char *str) {
  int i;

  for (i = 0; i < arr->count; i += 1) {
    if (strncmp(str, arr->elems[i], strlen(str)) == 0) {
      return 1;
    }
  }
  return 0;
}

void cstr_array_realloc(Cstr_array *arr, size_t new_size) {
  assert(arr->capacity < new_size);
  char **new_arr;

  new_arr = cmalloc(sizeof(char*) * new_size);

  memcpy(new_arr, arr->elems, sizeof(char*) * arr->count);
  free(arr->elems);
  arr->elems = new_arr;
  arr->capacity = new_size;
}

void cstr_array_remove(Cstr_array *arr, size_t index) {
  assert(index < arr->count);
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

  if (arr == NULL) {
    return;
  }

  if (arr->elems != NULL) {
    for (i = 0; i < arr->count; i += 1) {
      free(arr->elems[i]);
    }
    free(arr->elems);
  }
  memset(arr, 0, sizeof(Cstr_array));
}

void cstr_array_append(Cstr_array *arr, const char *const str) {
  char *dup;

  if (arr->count >= arr->capacity) {
    cstr_array_realloc(arr, arr->capacity > 0 ? arr->capacity * 2 : 8);
  }
  ERROR_NO((dup = strdup(str)) == NULL, "strdup(%s)", str);
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
    error(EXIT_FAILURE, errno, "stat(%s)", filepath);
  }
  return 1;
}

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
    error(EXIT_FAILURE, 0, "%s is not a valid number", str);
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
    cstr_array_from(&ctx.target_dirs, default_target_dirs, 1);
  } else {
    // TODO: do some research if modifying the argv is fine
    cstr_array_from(&ctx.target_dirs, argv + optind, argc - optind);
  }
}

void cleanup() {
  close_cache_stream();
  cstr_array_free_data(&ctx.invalid_files);
  cstr_array_free_data(&ctx.valid_files);
  cstr_array_loop(&ctx.target_dirs, dir, {
      free(dir);
    });
}

void set_file_mtime(const char *filepath, const struct timespec mtime) {
  struct timespec ts[2];
  struct stat statbuf;
  int file_fd;

  ERROR_NO((file_fd = open(filepath, O_RDONLY)) == -1, "open(%s)", filepath);
  ERROR_NO(fstat(file_fd, &statbuf) == -1, "fstat(%s)", filepath);

  ts[0] = statbuf.st_atim;
  ts[1] = mtime;

  ERROR_NO(futimens(file_fd, ts) == -1, "futimens(%s)", filepath);
  ERROR_NO(close(file_fd) == -1, "close(%s)", filepath);
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

void walk_tree(char *dirpath) {
  Cstr_array directories;
  Cstr_array files;
  DIR *dir;
  struct dirent *entry;
  char *path_buffer = cmalloc(ctx.PATH_LEN_MAX + 1);
  char *base_offset;

  *path_buffer = '\0';
  base_offset = stpcpy(path_buffer, dirpath);
  base_offset = stpcpy(base_offset, "/");

  cstr_array_free_data(&directories);
  cstr_array_free_data(&files);

  ERROR_NO((dir = opendir(dirpath)) == NULL, "opendir(%s)", dirpath);
  errno = 0;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    stpcpy(base_offset, entry->d_name);
    if (entry->d_type == DT_DIR) {
      cstr_array_append(&directories, path_buffer);
    } else if (entry->d_type == DT_REG) {
      cstr_array_append(&files, path_buffer);
    }
    *base_offset = '\0';
  }
  ERROR_NO(errno != 0, "readdir(%s)", dirpath);

  cstr_array_sort(&directories);
  cstr_array_sort(&files);

  printf("Directories\n");
  cstr_array_loop(&directories, dir, {
      printf("> %s\n", dir);
    });
  printf("Files\n");
  cstr_array_loop(&files, file, {
      printf("> %s\n", file);
    });

  closedir(dir);
  cstr_array_free_data(&directories);
  cstr_array_free_data(&files);
}

#ifdef CCHECK_TEST
int ccheck_main(int argc, char **argv) {
#else
int main(int argc, char **argv) {
#endif
  FILE *stream;
  char *line, *resolved_path;
  size_t linesz;

  memset(&ctx, 0, sizeof(Context)); // just to be sure that ctx data is set to zeros

  assert(argc > 0);
  ctx.binary_mtime = get_file_mtime(argv[0]); ctx.most_recent_mtime = ctx.binary_mtime;
  ctx.tab_width = 2;

  path_conf(ctx.NAME_LEN_MAX, _PC_NAME_MAX);
  path_conf(ctx.PATH_LEN_MAX, _PC_PATH_MAX);

  parse_arguments(argc, argv);

  // change target_dirs elements to absolute path
  cstr_array_loop(&ctx.target_dirs, dir, {
      resolved_path = cmalloc(ctx.PATH_LEN_MAX + 1);
      ERROR_NO(realpath(dir, resolved_path) == NULL, "realpath(%s)", dir);
      *dir_p = resolved_path;
    });

  cstr_array_sort(&ctx.target_dirs);

#ifndef CCHECK_TEST
  if (is_file_dir_exist(CACHE_FILE)) {
    stream = get_cache_stream();

    line = cmalloc(ctx.PATH_LEN_MAX + 1);
    *line = 0;
    errno = 0; // TODO: does errors even occured when fgets are used?
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

  cstr_array_loop(&ctx.target_dirs, dir, {
      // walk_tree(dir);
      ctx.current_dir = dir;
      ctx.current_dir_len = strnlen(dir, ctx.PATH_LEN_MAX);

      printf("%s:\n", dir);
      ERROR_NO(nftw(dir, process_entry, -1, 0) == -1, "nftw(%s)", dir);
    });

  set_file_mtime(argv[0], ctx.most_recent_mtime);
  cleanup();
  return 0;
}
