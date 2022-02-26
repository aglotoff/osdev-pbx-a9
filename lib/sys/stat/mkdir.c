#include <syscall.h>
#include <sys/stat.h>

int
mkdir(const char *path, mode_t mode)
{
  return __syscall(__SYS_MKNOD, (uintptr_t) path, S_IFDIR | mode, 0);
}
