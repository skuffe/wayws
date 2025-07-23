#include "util.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *xrealloc(void *p, size_t n) {
  void *q = realloc(p, n);
  if (!q) {
    perror("realloc");
    exit(1);
  }
  return q;
}
char *xstrdup(const char *s) {
  if (!s)
    return NULL;
  char *p = strdup(s);
  if (!p) {
    perror("strdup");
    exit(1);
  }
  return p;
}
int isnum(const char *s) {
  if (s == NULL || *s == '\0') { // Handle NULL or empty string
    return 0;
  }
  for (; *s; s++)
    if (!isdigit((unsigned char)*s))
      return 0;
  return 1;
}
void die(const char *msg) {
  fputs(msg, stderr);
  exit(1);
}
