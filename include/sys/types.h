#ifndef __SYS_TYPES_H__
#define __SYS_TYPES_H__

/** Used for device IDs. */
typedef short           dev_t;

/** Used for group IDs. */
typedef short           gid_t;

/** Used for file serial numbers. */
typedef unsigned long   ino_t;

/** Used for some file attributes. */
typedef unsigned short  mode_t;

/** Used for link counts. */
typedef short           nlink_t;

/** Used for file sizes. */
typedef unsigned long   off_t;

/** Used for process IDs and process group IDs. */
typedef int             pid_t;

/** Used for sizes of objects. */
typedef unsigned int    size_t;

/** Used for a count of bytes or an error indication. */
typedef long            ssize_t;

/** Used for time in seconds.*/
typedef long            time_t;

/** Used for user IDs. */
typedef short           uid_t;

#endif  // !__SYS_TYPES_H__
