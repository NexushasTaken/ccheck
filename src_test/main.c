#include "greetings.h"
#include "add.h"

int main() {
  hello_world();
  printf("%d + %d = %d\n", 4, 9, add(4, 9));
  return 0;
}
