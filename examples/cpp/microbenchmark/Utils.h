#include "../RDMASenderReceiver.h"
#include <stdlib.h> 
#include <stdio.h>
#include <unistd.h>
#include <thread>
#include <condition_variable>
#include <mutex>


int random(int min, int max) {
  static bool first = true;
  if (first) {
    srand(time(NULL));  // seeding for the first time only!
    first = false;
  }
  return min + rand() % ((max + 1) - min);
}