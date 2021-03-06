#ifndef __FCNTL_H__
#define __FCNTL_H__

/**
 * @file include/fcntl.h
 * 
 * File control options.
 */

#include <sys/stat.h>
#include <unistd.h>

#define O_RDONLY    (1 << 0)
#define O_WRONLY    (1 << 1)
#define O_RDWR      (O_RDONLY | O_WRONLY)
#define O_APPEND    (1 << 2)
#define O_CREAT     (1 << 3)
#define O_EXCL      (1 << 4)
#define O_NOCTTY    (1 << 5)
#define O_NONBLOCK  (1 << 6)
#define O_TRUNC     (1 << 7)

int creat(const char *, mode_t);
int open(const char *, int, ...);

#endif  // !__FCNTL_H__
