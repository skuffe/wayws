#include "util.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

void dbg_append(char *buf, size_t *len, size_t cap, const char *s) {
  while (*s && *len + 1 < cap)
    buf[(*len)++] = *s++;
}
void dbg_append_int(char *buf, size_t *len, size_t cap, int v) {
  char tmp[32];
  int tlen = 0;
  if (v == 0)
    tmp[tlen++] = '0';
  else {
    unsigned int u = (v < 0) ? (unsigned int)(-v) : (unsigned int)v;
    char rev[32];
    int rlen = 0;
    while (u) {
      rev[rlen++] = (char)('0' + (u % 10));
      u /= 10;
    }
    if (v < 0)
      tmp[tlen++] = '-';
    while (rlen--)
      tmp[tlen++] = rev[rlen];
  }
  for (int i = 0; i < tlen && *len + 1 < cap; ++i)
    buf[(*len)++] = tmp[i];
}
void dbg_append_hexptr(char *buf, size_t *len, size_t cap, uintptr_t p) {
  dbg_append(buf, len, cap, "0x");
  char hex[2 * sizeof p];
  int hlen = 0;
  for (int i = (int)(sizeof p * 2) - 1; i >= 0; --i) {
    int nib = (int)((p >> (i * 4)) & 0xF);
    hex[hlen++] = (char)(nib < 10 ? '0' + nib : 'a' + nib - 10);
  }
  for (int i = 0; i < hlen && *len + 1 < cap; ++i)
    buf[(*len)++] = hex[i];
}
