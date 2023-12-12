#include "../src/main.c"
#include <stdio.h>

#define CSTR_ARRAY_PRINT(array) cstr_array_print(__LINE__, array)
void cstr_array_print(int line, Cstr_array *array) {
  printf("{ line: %d\n", line);
  printf("  elems   : %p\n", array->elems);
  printf("  count   : %ld\n", array->count);
  printf("  capacity: %ld\n", array->capacity);
  printf("  elements: ");
  printf("[ ");
  for (int i = 0; i < array->count; i++) {
    printf("\"%s\", ", array->elems[i]);
  }
  printf("]\n");
  printf("}\n");
}

int main() {
  Cstr_array array = {0};
  CSTR_ARRAY_PRINT(&array);
  cstr_array_append(&array, "a");
  cstr_array_append(&array, "b");
  cstr_array_append(&array, "c");
  CSTR_ARRAY_PRINT(&array);
  cstr_array_remove(&array, 0);
  cstr_array_remove(&array, 1);
  cstr_array_remove(&array, 0);
  CSTR_ARRAY_PRINT(&array);
  cstr_array_append(&array, "1");
  CSTR_ARRAY_PRINT(&array);
  cstr_array_append(&array, "2");
  CSTR_ARRAY_PRINT(&array);
  cstr_array_append(&array, "3");
  CSTR_ARRAY_PRINT(&array);
  cstr_array_append(&array, "4");
  CSTR_ARRAY_PRINT(&array);
  cstr_array_append(&array, "5");
  CSTR_ARRAY_PRINT(&array);
  cstr_array_append(&array, "6");
  CSTR_ARRAY_PRINT(&array);
  cstr_array_append(&array, "7");
  CSTR_ARRAY_PRINT(&array);
  cstr_array_append(&array, "8");
  CSTR_ARRAY_PRINT(&array);
  return 0;
}
