#include "../src/main.c"
#include "test.h"

static void test_relative_dir(void **state) {
  (void)state;
#define grp(rel, path) (get_relative_dir(rel, path))
#define ase(s1, s2)    (assert_string_equal(s1, s2))
  ase(grp("/a/b/c/d/e", "/"), "../../../../..");
  ase(grp("/a/b/c/d/e", "/a"), "../../../..");
  ase(grp("/a/b/c/d/e", "/a/b"), "../../..");
  ase(grp("/a/b/c/d/e", "/a/b/c"), "../..");
  ase(grp("/a/b/c/d/e", "/a/b/c/d"), "..");
  ase(grp("/a/b/c/d/e", "/a/b/c/d/e"), ".");

  ase(grp("/a/b/c/d", "/"), "../../../..");
  ase(grp("/a/b/c/d", "/a"), "../../..");
  ase(grp("/a/b/c/d", "/a/b"), "../..");
  ase(grp("/a/b/c/d", "/a/b/c"), "..");
  ase(grp("/a/b/c/d", "/a/b/c/d"), ".");
  ase(grp("/a/b/c/d", "/a/b/c/d/e"), "./e");

  ase(grp("/a/b/c", "/"), "../../..");
  ase(grp("/a/b/c", "/a"), "../..");
  ase(grp("/a/b/c", "/a/b"), "..");
  ase(grp("/a/b/c", "/a/b/c"), ".");
  ase(grp("/a/b/c", "/a/b/c/d"), "./d");
  ase(grp("/a/b/c", "/a/b/c/d/e"), "./d/e");

  ase(grp("/a/b", "/"), "../..");
  ase(grp("/a/b", "/a"), "..");
  ase(grp("/a/b", "/a/b"), ".");
  ase(grp("/a/b", "/a/b/c"), "./c");
  ase(grp("/a/b", "/a/b/c/d"), "./c/d");
  ase(grp("/a/b", "/a/b/c/d/e"), "./c/d/e");

  ase(grp("/a", "/"), "..");
  ase(grp("/a", "/a"), ".");
  ase(grp("/a", "/a/b"), "./b");
  ase(grp("/a", "/a/b/c"), "./b/c");
  ase(grp("/a", "/a/b/c/d"), "./b/c/d");
  ase(grp("/a", "/a/b/c/d/e"), "./b/c/d/e");

  ase(grp("/", "/"), ".");
  ase(grp("/", "/a"), "./a");
  ase(grp("/", "/a/b"), "./a/b");
  ase(grp("/", "/a/b/c"), "./a/b/c");
  ase(grp("/", "/a/b/c/d"), "./a/b/c/d");
  ase(grp("/", "/a/b/c/d/e"), "./a/b/c/d/e");
}

int main(int argc, char **argv) {
  init(argc, argv);
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_relative_dir),
  };
  cleanup();
  return cmocka_run_group_tests(tests, NULL, NULL);
}

/*

a/b/1/2/file.txt
a/b/c/d/file.txt

*/
