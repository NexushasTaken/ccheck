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

int str_ends_with(const char *cstr, const char *postfix) {
  const size_t cstr_len = strlen(cstr);
  const size_t postfix_len = strlen(postfix);
  return postfix_len <= cstr_len
    && strcmp(cstr + cstr_len - postfix_len, postfix) == 0;
}

struct timespec get_file_mtime(const char *filepath) {
  struct stat statbuf;

  ASSERT_ERR(stat(filepath, &statbuf), "could not stat %s", filepath);
  return statbuf.st_mtim;
}

mode_t get_file_mode(const char *filepath) {
  struct stat statbuf;

  ASSERT_ERR(stat(filepath, &statbuf), "could not stat %s", filepath);
  return statbuf.st_mode;
}

typedef struct {
  struct timespec binary_mtime;
  struct timespec most_recent_mtime;

  int level_deep; // used for recursive function
  int indent_width;
} Context;

static Context ctx;
#define AINFO_INDENT(end, ...) AINFO(ctx.level_deep * ctx.indent_width, end, __VA_ARGS__)

void init(int argc, char **argv) {
  assert(argc > 0);
  ctx.binary_mtime = get_file_mtime(argv[0]);
  ctx.most_recent_mtime = ctx.binary_mtime;
  ctx.indent_width = 2;
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

void check_src_syntax(const char *filepath) {
  struct timespec sec;

  if (str_ends_with(filepath, ".c") ||
      str_ends_with(filepath, ".h")) {
    AINFO_INDENT(" ", "%s", filepath);
    sec = get_file_mtime(filepath);
    if (sec.tv_sec > ctx.binary_mtime.tv_sec) {
      sleep(1);
      printf("- done");
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

void traverse_directory(const char *dirpath) {
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

int main(int argc, char **argv) {
  if (argc != 2) {
    PANIC("usage: %s <filename/directory>", argv[0]);
  }

  init(argc, argv);

  const char *target = ".";
  target = argv[1];
  if (S_ISDIR(get_file_mode(target))) {
    traverse_directory(target);
  } else {
    PANIC("%s is not a directory", target);
  }

  set_file_mtime(argv[0], ctx.most_recent_mtime);
  return 0;
}
