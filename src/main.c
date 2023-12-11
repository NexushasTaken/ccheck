#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
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

static struct timespec binary_mtime;
static struct timespec most_recent_mtime;

#define TIMESPEC_TO_TIMEVAL(ts, tv) \
  do {                              \
    tv.tv_sec = ts.tv_sec;          \
    tv.tv_usec = ts.tv_nsec/1000;   \
  } while (0)

#define print_timespec(ts) printf("%ld.%ld", ts.tv_sec, ts.tv_nsec)

struct timespec get_file_mtime(const char *filepath) {
  struct stat statbuf;
  ASSERT_ERR(stat(filepath, &statbuf), "could not stat %s", filepath);
  return statbuf.st_mtim;
}

void print_amtimespec(const char *msg, struct timespec ts[2]) {
  printf("[INFO] %s : %ld.%ld\n", msg, ts[1].tv_sec, ts[1].tv_nsec);
}

void set_file_mtime(const char *filepath, const struct timespec mtime) {
  struct timespec ts[2];
  struct stat statbuf;
  int file_fd;

  ASSERT_ERR((file_fd = open(filepath, O_RDONLY)), "could not open %s", filepath);
  ASSERT_ERR(fstat(file_fd, &statbuf), "could not stat %s", filepath);
  ts[0] = statbuf.st_atim;
  ts[1] = statbuf.st_mtim;

  print_amtimespec("before", ts);
  ts[1] = mtime;

  ASSERT_ERR(futimens(file_fd, ts), "could not change the timestamp of %s", filepath);

#if 1
  ASSERT_ERR(fstat(file_fd, &statbuf), "could not stat %s", filepath);
  ts[0] = statbuf.st_atim;
  ts[1] = statbuf.st_mtim;
#endif
  print_amtimespec("after ", ts);

  ASSERT_ERR(close(file_fd), "could not file descriptor %d", file_fd);
}

void check_src_syntax(const char *filepath) {
  if (str_ends_with(filepath, ".c") ||
      str_ends_with(filepath, ".h")) {
    struct timespec sec = get_file_mtime(filepath);
    if (sec.tv_sec > binary_mtime.tv_sec) {
      printf("%s: newer\n", filepath);
    }
    if (sec.tv_sec > most_recent_mtime.tv_sec) {
      most_recent_mtime = sec;
    }
  }
}

#define CHDIR(dir)                                 \
  do {                                             \
    if (chdir(dir) != 0) {                         \
      PANIC("could not change current directory"); \
    }                                              \
    INFO("CWD changed to %s", dir);                \
  } while (0) 

long path_max() {
  errno = 0;
  long max = pathconf(".", _PC_PATH_MAX);
  if (max < 0) {
    PANIC("could not get PATH_MAX");
  }
  return max;
}

mode_t get_file_mode(const char *filepath) {
  struct stat statbuf;
  ASSERT_ERR(stat(filepath, &statbuf), "could not stat %s", filepath);
  return statbuf.st_mode;
}

void print_cwd() {
  char cwd[path_max()+1];
  getcwd(cwd, path_max());
  INFO("CWD: %s\n", cwd);
}

void traverse_directory(const char *dirpath) {
  struct dirent *entry_buf;
  mode_t file_mode;
  DIR *parent;

  errno = 0;
  parent = opendir(dirpath);  
  ASSERT_NULL(parent, "could not open directory %s", dirpath);

  INFO("checking %s\n", dirpath);
  CHDIR(dirpath);
  
  while ((entry_buf = readdir(parent)) != NULL) {
    if (strcmp(entry_buf->d_name, ".") == 0 ||
        strcmp(entry_buf->d_name, "..") == 0) {
      continue;
    }
    file_mode = get_file_mode(entry_buf->d_name);
    INFO("entry: %s", entry_buf->d_name);
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
  CHDIR("..");
}

int main(int argc, char **argv) {
  if (argc != 2) {
    PANIC("usage: %s <filename/directory>", argv[0]);
  }
  
  binary_mtime = get_file_mtime(argv[0]);

  const char *target = ".";
  target = argv[1];
  if (S_ISDIR(get_file_mode(target))) {
    traverse_directory(target);
  } else {
    PANIC("%s is not a directory", target);
  }

  printf("most_recent_mtime: ");
  print_timespec(most_recent_mtime);
  printf("\n");
  set_file_mtime(argv[0], most_recent_mtime);
  return 0;
}
