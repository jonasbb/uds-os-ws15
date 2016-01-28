#ifndef FILESYS_FILE_STRUCT_H
#define FILESYS_FILE_STRUCT_H

#include <stdbool.h>
#include <stddef.h>

/* Only directories have a parent set. For a directory the deny_write has no meaning */
struct file
  {
    struct inode *inode;        /* File's inode. */
    off_t pos;                  /* Current position. */
    bool deny_write;            /* Has file_deny_write() been called? */
    struct file *parent;        /* parent node */
  };

#endif
