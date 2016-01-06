#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "filesys/file.h"

void syscall_init (void);
typedef int mapid_t;

struct mmapdata {
  mapid_t mapid;
  void *base_addr;
  size_t pgcount;
  struct file *file;
};

#endif /* userprog/syscall.h */
