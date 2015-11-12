#include "userprog/syscall.h"
#include <stdio.h>
#include <stdbool.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "lib/user/syscall.h"

static void syscall_handler (struct intr_frame *);
static void validate_user_string (const char* user_str);
static void validate_user_buffer (const void* user_buf, unsigned size);
static void* uaddr_to_kaddr (const void* uaddr);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void
halt(void) {

}

void 
exit (int status) {

}

pid_t
exec(const char *cmd_line) {
  return -1;
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
filesize (int fd) {
  return -1;
}

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

}

uint32_t 
tell (int fd) {
  return 1;
}

void 
close (int fd) {
  return;
}

/*
 * Validates that every byte of a user provided char* is
 * inside the user's mapped memory.
 * Terminates the user process on address violations.
 *
 * Takes a char* into userspace to validate.
 */
static void
validate_user_string (const char* user_str)
{
  // validate original pointer
  char* kernel = uaddr_to_kaddr(user_str);
  uintptr_t current_page = pg_no(user_str);
  
  char* user = user_str;
  for (;*kernel;)
  {
    // move pointers to next char
    user++;
    kernel++;
    
    // page changed
    if (pg_no(user) != current_page)
    {
      //validate pointer again, because page has changed
      kernel = uaddr_to_kaddr(user);
      current_page = pg_no(user);
    }
  }
}

/*
 * Validates that every byte of a user provided buffer is
 * inside the user's mapped memory.
 * Terminates the user process on address violations.
 *
 * Takes a void* into userspace to validate.
 * The length of the buffer.
 */
static void
validate_user_buffer (const void* user_buf, unsigned size)
{
  // validate original pointer
  uaddr_to_kaddr(user_buf);
  void* user = user_buf;
  
  // bytes remaining in page
  unsigned remaining_bytes = PGSIZE - pg_ofs(user);
  if (remaining_bytes > size)
  {
    // buffer length is longer than the rest of the page
    size -= remaining_bytes;
    user += remaining_bytes;
  }
  else
  {
    // buffer is inside one page, everything is correct
    return;
  }
  
  // as long as size is at least one page, the buffer reaches
  // a new page which we have to check also
  for (; size > PGSIZE;)
  {
    // move pointer to next page
    size -= PGSIZE;
    user += PGSIZE;
    
    // validate pointer
    uaddr_to_kaddr(user_buf);
  }
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
                   char* file_name = (char*) uaddr_to_kaddr(file_name_uaddr); /* char pointer in kernel mode */
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

static void* 
uaddr_to_kaddr (const void* uaddr) {
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

