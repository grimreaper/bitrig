// RUN: %clangxx_tsan -O1 %s -o %t && %deflake %run %t | FileCheck %s
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int x[4], z[4];

void *MemCpyThread(void *a) {
  memcpy((int*)a, z, 16);
  return NULL;
}

void *MemSetThread(void *a) {
  sleep(1);
  memset((int*)a, 0, 16);
  return NULL;
}

int main() {
  pthread_t t[2];
  // Race on x between memcpy and memset
  pthread_create(&t[0], NULL, MemCpyThread, x);
  pthread_create(&t[1], NULL, MemSetThread, x);
  pthread_join(t[0], NULL);
  pthread_join(t[1], NULL);
  printf("PASS\n");
  return 0;
}

// CHECK: WARNING: ThreadSanitizer: data race
// CHECK:   #0 memset
// CHECK:   #1 MemSetThread
// CHECK:  Previous write
// CHECK:   #0 memcpy
// CHECK:   #1 MemCpyThread

