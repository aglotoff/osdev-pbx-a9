#include <syscall.h>
#include <unistd.h>

/**
 * Create a new process.
 */
pid_t
fork(void)
{
  return __syscall(__SYS_FORK, 0, 0, 0);
}
