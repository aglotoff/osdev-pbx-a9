#ifndef __KERNEL_FS_H__
#define __KERNEL_FS_H__

#include <stdint.h>

#include "elf.h"
#include "ext2.h"
#include "list.h"
#include "process.h"
#include "sleeplock.h"

#define INODE_CACHE_SIZE  32

struct Inode {
  unsigned         num;
  int              valid;
  int              ref_count;
  struct ListLink  cache_link;
  struct Sleeplock lock;
  struct WaitQueue wait_queue;
  
  struct Ext2Inode data;
};

void fs_init(void);

#endif  // !__KERNEL_FS_H__