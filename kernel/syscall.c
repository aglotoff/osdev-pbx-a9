#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <syscall.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include <cprintf.h>
#include <cpu.h>
#include <drivers/rtc.h>
#include <fs/file.h>
#include <fs/fs.h>
#include <mm/vm.h>
#include <process.h>
#include <types.h>
#include <cprintf.h>

#include <sys.h>

static int     sys_get_num(void);
static int32_t sys_get_arg(int);

static int32_t (*syscalls[])(void) = {
  [__SYS_FORK]     = sys_fork,
  [__SYS_EXEC]     = sys_exec,
  [__SYS_WAIT]     = sys_wait,
  [__SYS_EXIT]     = sys_exit,
  [__SYS_GETPID]   = sys_getpid,
  [__SYS_GETPPID]  = sys_getppid,
  [__SYS_TIME]     = sys_time,
  [__SYS_GETDENTS] = sys_getdents,
  [__SYS_CHDIR]    = sys_chdir,
  [__SYS_FCHDIR]   = sys_fchdir,
  [__SYS_OPEN]     = sys_open,
  [__SYS_UMASK]    = sys_umask,
  [__SYS_MKNOD]    = sys_mknod,
  [__SYS_LINK]     = sys_link,
  [__SYS_UNLINK]   = sys_unlink,
  [__SYS_RMDIR]    = sys_rmdir,
  [__SYS_STAT]     = sys_stat,
  [__SYS_CLOSE]    = sys_close,
  [__SYS_READ]     = sys_read,
  [__SYS_WRITE]    = sys_write,
  [__SYS_SBRK]     = sys_sbrk,
  [__SYS_UNAME]    = sys_uname,
  [__SYS_CHMOD]    = sys_chmod,
};

int32_t
sys_dispatch(void)
{
  int num;

  if ((num = sys_get_num()) < 0)
    return num;

  if ((num < (int) ARRAY_SIZE(syscalls)) && syscalls[num])
    return syscalls[num]();

  cprintf("Unknown system call %d\n", cpu_id(), num);
  return -ENOSYS;
}

/*
 * ----------------------------------------------------------------------------
 * Helper functions
 * ----------------------------------------------------------------------------
 */

// Extract the system call number from the SVC instruction opcode
static int
sys_get_num(void)
{
  struct Process *current = my_process();
  int *pc = (int *) (current->tf->pc - 4);
  int r;

  if ((r = vm_user_check_buf(current->vm, pc, sizeof(int), VM_READ)) < 0)
    return r;

  return *pc & 0xFFFFFF;
}

// Get the n-th argument from the current process' trap frame.
static int32_t
sys_get_arg(int n)
{
  struct Process *current = my_process();

  switch (n) {
  case 0:
    return current->tf->r0;
  case 1:
    return current->tf->r1;
  case 2:
    return current->tf->r2;
  case 3:
    return current->tf->r3;
  default:
    if (n < 0)
      panic("Invalid argument number: %d", n);

    // TODO: grab additional parameters from the user's stack.
    return 0;
  }
}

/**
 * Fetch the nth system call argument as an integer.
 * 
 * @param n The argument number.
 * @param ip Pointer to the memory address to store the argument value.
 * 
 * @retval 0 on success.
 * @retval -EINVAL if an invalid argument number is specified.
 */
static int
sys_arg_int(int n, int *ip)
{
  *ip = sys_get_arg(n);
  return 0;
}

/**
 * Fetch the nth system call argument as a long integer.
 * 
 * @param n The argument number.
 * @param ip Pointer to the memory address to store the argument value.
 * 
 * @retval 0 on success.
 * @retval -EINVAL if an invalid argument number is specified.
 */
static int
sys_arg_short(int n, short *ip)
{
  *ip = (short) sys_get_arg(n);
  return 0;
}

static int
sys_arg_long(int n, long *ip)
{
  *ip = (long) sys_get_arg(n);
  return 0;
}

/**
 * Fetch the nth system call argument as pointer to a buffer of the specified
 * length. Check that the pointer is valid and the user has right permissions.
 * 
 * @param n    The argument number.
 * @param pp   Pointer to the memory address to store the argument value.
 * @param len  The length of the memory region pointed to.
 * @param perm The permissions to be checked.
 * 
 * @retval 0 on success.
 * @retval -EFAULT if the arguments doesn't point to a valid memory region.
 */
static int32_t
sys_arg_buf(int n, void **pp, size_t len, int perm)
{ 
  void *ptr = (void *) sys_get_arg(n);
  int r;

  if ((r = vm_user_check_buf(my_process()->vm, ptr, len, perm)) < 0)
    return r;

  *pp = ptr;

  return 0;
}

/**
 * Fetch the nth system call argument as a string pointer. Check that the
 * pointer is valid, the user has right permissions and the string is
 * null-terminated.
 * 
 * @param n    The argument number.
 * @param strp Pointer to the memory address to store the argument value.
 * @param perm The permissions to be checked.
 * 
 * @retval 0 on success.
 * @retval -EFAULT if the arguments doesn't point to a valid string.
 */
static int32_t
sys_arg_str(int n, const char **strp, int perm)
{
  char *str = (char *) sys_get_arg(n);
  int r;

  if ((r = vm_user_check_str(my_process()->vm, str, perm)) < 0)
    return r;

  *strp = str;

  return 0;
}

/**
 * Fetch the nth system call argument as a file descriptor. Check that the
 * file desriptor is valid.
 * 
 * @param n       The argument number.
 * @param fdstore Pointer to the memory address to store the file descriptor.
 * @param fstore  Pointer to the memory address to store the file.
 * 
 * @retval 0 on success.
 * @retval -EFAULT if the arguments doesn't point to a valid string.
 * @retval -EBADF if the file descriptor is invalid.
 */
static int
sys_arg_fd(int n, int *fdstore, struct File **fstore)
{
  struct Process *current = my_process();
  int fd = sys_get_arg(n);

  if ((fd < 0) || (fd >= OPEN_MAX) || (current->files[fd] == NULL))
    return -EBADF;
  
  if (fdstore)
    *fdstore = fd;
  if (fstore)
    *fstore = current->files[fd];

  return 0;
}

static int
sys_arg_args(int n, char ***store)
{
  struct Process *current = my_process();
  char **args;
  int i;

  args = (char **) sys_get_arg(n);
  
  for (i = 0; ; i++) {
    int r;

    if ((r = vm_user_check_buf(current->vm, args + i, sizeof(args[i]),
                               VM_READ)) < 0)
      return r;

    if (args[i] == NULL)
      break;
    
    if ((r = vm_user_check_str(current->vm, args[i], VM_READ)) < 0)
      return r;
  }

  if (store)
    *store = args;

  return 0;
}

/*
 * ----------------------------------------------------------------------------
 * System Call Implementations
 * ----------------------------------------------------------------------------
 */

int32_t
sys_fork(void)
{
  return process_copy();
}

int32_t
sys_exec(void)
{
  const char *path;
  char **argv, **envp;
  int r;

  if ((r = sys_arg_str(0, &path, VM_READ)) < 0)
    return r;
  if ((r = sys_arg_args(1, &argv)) < 0)
    return r;
  if ((r = sys_arg_args(2, &envp)) < 0)
    return r;

  return process_exec(path, argv, envp);
}

int32_t
sys_wait(void)
{
  pid_t pid;
  int *stat_loc;
  int r;
  
  if ((r = sys_arg_int(0, &pid)) < 0)
    return r;

  if ((r = sys_arg_buf(1, (void **) &stat_loc, sizeof(int), VM_WRITE)) < 0)
    return r;

  return process_wait(pid, stat_loc, 0);
}

int32_t
sys_exit(void)
{
  int status, r;
  
  if ((r = sys_arg_int(0, &status)) < 0)
    return r;

  process_destroy(status);

  return 0;
}

int32_t
sys_getpid(void)
{
  return my_process()->pid;
}

int32_t
sys_getppid(void)
{
  return my_process()->parent ? my_process()->parent->pid : my_process()->pid;
}

int32_t
sys_time(void)
{
  return rtc_time();
}

int32_t
sys_getdents(void)
{
  void *buf;
  size_t n;
  struct File *f;
  int r;

  if ((r = sys_arg_fd(0, NULL, &f)) < 0)
    return r;

  if ((r = sys_arg_int(2, (int *) &n)) < 0)
    return r;

  if ((r = sys_arg_buf(1, &buf, n, VM_READ)) < 0)
    return r;

  return file_getdents(f, buf, n);
}

int32_t
sys_chdir(void)
{
  const char *path;
  struct Inode *ip;
  int r;

  if ((r = sys_arg_str(0, &path, VM_READ)) < 0)
    return r;
  
  if ((r = fs_name_lookup(path, &ip)) < 0)
    return r;
  
  return fs_chdir(ip);
}

int32_t
sys_chmod(void)
{
  const char *path;
  mode_t mode;
  struct Inode *ip;
  int r;

  if ((r = sys_arg_str(0, &path, VM_READ)) < 0)
    return r;
  if ((r = sys_arg_short(1, (short *) &mode)) < 0)
    return r;

  if ((r = fs_name_lookup(path, &ip)) < 0)
    return r;

  r = fs_chmod(ip, mode);

  fs_inode_put(ip);

  return r;
}

int32_t
sys_fchdir(void)
{
  struct File *f;
  int r;

  if ((r = sys_arg_fd(0, NULL, &f)) < 0)
    return r;

  if (f->type != FD_INODE)
    return -ENOTDIR;

  return fs_chdir(f->inode);
}

static int
fd_alloc(struct File *f)
{
  struct Process *current = my_process();
  int i;

  for (i = 0; i < OPEN_MAX; i++)
    if (current->files[i] == NULL) {
      current->files[i] = f;
      return i;
    }
  return -EMFILE;
}

int32_t
sys_open(void)
{
  struct File *f;
  const char *path;
  int oflag, r;
  mode_t mode;

  if ((r = sys_arg_str(0, &path, VM_READ)) < 0)
    return r;
  if ((r = sys_arg_int(1, &oflag)) < 0)
    return r;
  if ((r = sys_arg_short(2, (short *) &mode)) < 0)
    return r;

  if ((r = file_open(path, oflag, mode, &f)) < 0)
    return r;

  if ((r = fd_alloc(f)) < 0)
    file_close(f);

  return r;
}

int32_t
sys_umask(void)
{
  struct Process *proc = my_process();
  mode_t cmask;
  int r;

  if ((r = sys_arg_short(0, (short *) &cmask)) < 0)
    return r;

  r = proc->cmask & (S_IRWXU | S_IRWXG | S_IRWXO);
  proc->cmask = cmask;

  return r;
}

int32_t
sys_link(void)
{
  const char *path1, *path2;
  int r;

  if ((r = sys_arg_str(0, &path1, VM_READ)) < 0)
    return r;
  if ((r = sys_arg_str(1, &path2, VM_READ)) < 0)
    return r;

  return fs_link((char *) path1, (char *) path2);
}

int32_t
sys_mknod(void)
{
  const char *path;
  mode_t mode;
  dev_t dev;
  int r;

  if ((r = sys_arg_str(0, &path, VM_READ)) < 0)
    return r;
  if ((r = sys_arg_short(1, (short *) &mode)) < 0)
    return r;
  if ((r = sys_arg_short(2, &dev)) < 0)
    return r;

  return fs_create(path, mode, dev, NULL);
}

int32_t
sys_unlink(void)
{
  const char *path;
  int r;

  if ((r = sys_arg_str(0, &path, VM_READ)) < 0)
    return r;

  return fs_unlink(path);
}

int32_t
sys_rmdir(void)
{
  const char *path;
  int r;

  if ((r = sys_arg_str(0, &path, VM_READ)) < 0)
    return r;

  return fs_rmdir(path);
}

int32_t
sys_stat(void)
{
  struct File *f;
  struct stat *buf;
  int r;

  if ((r = sys_arg_fd(0, NULL, &f)) < 0)
    return r;

  if ((r = sys_arg_buf(1, (void **) &buf, sizeof(*buf), VM_WRITE)) < 0)
    return r;

  return file_stat(f, buf);
}

int32_t
sys_close(void)
{
  struct File *f;
  int r, fd;

  if ((r = sys_arg_fd(0, &fd, &f)) < 0)
    return r;

  file_close(f);
  my_process()->files[fd] = NULL;

  return 0;
}

int32_t
sys_read(void)
{
  void *buf;
  size_t n;
  struct File *f;
  int r;

  if ((r = sys_arg_fd(0, NULL, &f)) < 0)
    return r;

  if ((r = sys_arg_int(2, (int *) &n)) < 0)
    return r;

  if ((r = sys_arg_buf(1, &buf, n, VM_READ)) < 0)
    return r;

  return file_read(f, buf, n);
}

int32_t
sys_write(void)
{
  void *buf;
  size_t n;
  struct File *f;
  int r;

  if ((r = sys_arg_fd(0, NULL, &f)) < 0)
    return r;

  if ((r = sys_arg_int(2, (int *) &n)) < 0)
    return r;

  if ((r = sys_arg_buf(1, &buf, n, VM_WRITE)) < 0)
    return r;

  return file_write(f, buf, n);
}

int32_t
sys_sbrk(void)
{
  ptrdiff_t n;
  int r;

  if ((r = sys_arg_long(0, &n)) < 0)
    return r;
  
  return (int32_t) process_grow(n);
}

int32_t
sys_uname(void)
{
  extern struct utsname utsname;  // defined in main.c

  struct utsname *name;
  int r;

  if ((r = sys_arg_buf(0, (void **) &name, sizeof(*name), VM_WRITE)) < 0)
    return r;
  
  memcpy(name, &utsname, sizeof (*name));

  return 0;
}
