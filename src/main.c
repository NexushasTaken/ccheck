#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include "logger.h"

int str_ends_with(const char *cstr, const char *postfix) {
  const size_t cstr_len = strlen(cstr);
  const size_t postfix_len = strlen(postfix);
  return postfix_len <= cstr_len
    && strcmp(cstr + cstr_len - postfix_len, postfix) == 0;
}

void check_src_syntax(const char *filepath) {
  if (!str_ends_with(filepath, ".c")) {
    return;
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

  ASSERT_ERR(errno, "could not read directory");
  closedir(parent);
  CHDIR("..");
}

int main(int argc, char **argv) {
  if (argc != 2) {
    PANIC("usage: %s <filename/directory>", argv[0]);
  }
  const char *target = ".";
  target = argv[1];
  if (S_ISDIR(get_file_mode(target))) {
    traverse_directory(target);
  } else {
    PANIC("%s is not a directory", target);
  }
  return 0;
}
