#include <stdio.h>
#include <unistd.h>

int main() {
  FILE *file = fopen("lock-test.txt", "w");
  if (file == NULL) {
    perror("Error opening file");
    return 1;
  }
  while (1) {
    fprintf(file, "test\n");
    sleep(3);
  }
  fclose(file);
  return 0;
}
