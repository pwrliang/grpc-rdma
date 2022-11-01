#include "mb.h"
#define TABSIZE 256 * 1024 * 1024

int main(int argc, char* argv[]) {
  int cpu_id = atoi(argv[1]);
  printf("Grab CPU: %d\n", cpu_id);
  bind_thread_to_core(cpu_id);
  while (true) {
    int size = random() % TABSIZE;
    int* tab = new int[size];

    for (int i = 0; i < size; i += 1) tab[i] *= i;

    delete[] tab;
  }
}