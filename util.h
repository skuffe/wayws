#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
int isnum(const char *s);
void die(const char *msg);

#endif // UTIL_H
