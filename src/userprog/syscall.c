#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void
halt(void) {
  return;
}

void 
exit (int status) {
  return -1;
}

pid_t
exec(const char *cmd_line) {
  return;
}

int 
wait (pid_t pid) {
  return -1;
}

bool 
create (const char *file, uint32_t initial_size) {
  return false;
}

bool 
remove (const char *file) {
  return false;
}

int 
open (const char *file) {
  return -1;
}

int 
filesize (int fd)

int 
read (int fd, void *buffer, uint32_t size) {
  return -1;
}

int 
write (int fd, const void *buffer, uint32_t size) {
  return -1;
}

void 
seek (int fd, uint32_t position) {
  return;
}

uint32_t 
tell (int fd) {
  return 1;
}

void 
close (int fd) {
  return;
}

static void
syscall_handler (struct intr_frame *f) 
{
  printf ("system call!\n");

  uint32_t syscall_nr = *((uint32_t*) uaddr_to_kaddr(f->esp));
  
  switch (syscall_nr) {
    case SYS_HALT: halt(); break;                  /* Halt the operating system. */
    case SYS_EXIT: ;int status = *((uint32_t*) uaddr_to_kaddr(f->esp+4));
                   exit(status); break;                  /* Terminate this process. */
    case SYS_EXEC: ;char* exec_name_uaddr = *((char**) uaddr_to_kaddr(f->esp+4)); /* char pointer in usermode */ 
                   char* exec_name = (char*) uaddr_to_kaddr(exec_name_uaddr); /* char pointer in kernel mode */
                   f->eax = exec(exec_name);
                   break; /* Start another process. */
    case SYS_WAIT: ;int pid = *((uint32_t*) uaddr_to_kaddr(f->esp+4));
                   f->eax = wait(pid);  /* Wait for a child process to die. */
                   break;
    case SYS_CREATE: 
                   ;char* file_name_uaddr = *((char**) uaddr_to_kaddr(f->esp+4)); /* char pointer in usermode */ 
                   char* file_name = (char*) uaddr_to_kaddr(exec_name_uaddr); /* char pointer in kernel mode */
                   uint32_t initial_size = *((uint32_t*) uaddr_to_kaddr(f->esp+8));
                   f->eax = create(file_name, initial_size);
                   break;                /* Create a file. */
    case SYS_REMOVE:
                   file_name_uaddr = *((char**) uaddr_to_kaddr(f->esp+4)); /* char pointer in usermode */ 
                   file_name = (char*) uaddr_to_kaddr(exec_name_uaddr); /* char pointer in kernel mode */
                   f->eax = remove(file_name);
                   break;                 /* Delete a file. */
    case SYS_OPEN: 
                   file_name_uaddr = *((char**) uaddr_to_kaddr(f->esp+4)); /* char pointer in usermode */ 
                   file_name = (char*) uaddr_to_kaddr(exec_name_uaddr); /* char pointer in kernel mode */
                   f->eax = open(file_name);
                   break;                  /* Open a file. */
    case SYS_FILESIZE: 
                   ;int fd = *((uint32_t*) uaddr_to_kaddr(f->esp+4));
                   f->eax = filesize(fd);
                   break;              /* Obtain a file's size. */
    case SYS_READ: 
                   fd = *((uint32_t*) uaddr_to_kaddr(f->esp+4));
                   void* buffer;
                   uint32_t size;
                   f->eax = read(fd, buffer, size);
                   break;    /* Read from a file. */
    case SYS_WRITE:
                   fd = *((uint32_t*) uaddr_to_kaddr(f->esp+4));
                   buffer;
                   //size;
                   f->eax = write(fd, buffer, size);
                   break;                  /* Write to a file. */
    case SYS_SEEK: 
                   fd = *((uint32_t*) uaddr_to_kaddr(f->esp+4));
                   uint32_t position = *((uint32_t*) uaddr_to_kaddr(f->esp+8));
                   seek(fd,position); 
                   break;  /* Change position in a file. */
    case SYS_TELL: 
                   fd = *((uint32_t*) uaddr_to_kaddr(f->esp+4));
                   f->eax = tell(fd);
                   break;                  /* Report current position in a file. */
    case SYS_CLOSE:
                   fd = *((uint32_t*) uaddr_to_kaddr(f->esp+4));
                   close(fd); 
                   break;                  /* Close a file. */
    default: thread_exit (); break; /* Should not happen */ 
  }
    
}

void* 
uaddr_to_kaddr (void* uaddr) {
  if (is_user_vaddr(uaddr)){
    void* page = pagedir_get_page(thread_current()->pagedir, uaddr);
    if (page) {
      return page;
    } 
    else {
      exit(-1); /* address violation */
    }
  }
  else {
    exit(-1); /* address violation */
  }
}

