#ifndef __KERNEL_FS_BUF_H__
#define __KERNEL_FS_BUF_H__

#ifndef __KERNEL__
#error "This is a kernel header; user programs should not #include it"
#endif

/**
 * @file kernel/buf.h
 * 
 * Buffer cache layer of the filesystem.
 */

#include <stdint.h>
#include <sys/types.h>

#include <list.h>
#include <sync.h>

#define BLOCK_SIZE      1024        ///< Size of a single filesystem block

/**
 * 
 * 
 * TODO: allow variable block sizes.
 */
struct Buf {
  unsigned long   block_no;         ///< Filesystem block number
  dev_t           dev;              ///< ID of the device this block belongs to
  int             flags;            ///< Status flags
  int             ref_count;        ///< The number of references to the block
  struct ListLink cache_link;       ///< Link into the buf cache
  struct ListLink queue_link;       ///< Link into the driver queue
  struct ListLink wait_queue;       ///< Processes waiting for the block data
  struct Mutex    mutex;            ///< Mutex protecting the block data
  size_t          block_size;       ///< Must be BLOCK_SIZE
  uint8_t         data[BLOCK_SIZE]; ///< Block data
};

// Buffer status flags
#define BUF_VALID   (1 << 0)  ///< Buffer has been read from the disk
#define BUF_DIRTY   (1 << 1)  ///< Buffer needs to be written to the disk

void        buf_init(void);
struct Buf *buf_read(unsigned, dev_t);
void        buf_write(struct Buf *);
void        buf_release(struct Buf *);

#endif  // !__KERNEL_FS_BUF_H__
