#include <stdio.h>
#include <debug.h>
#include <stdbool.h>
#include <string.h>
#include <syscall-nr.h>
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/directory.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "lib/user/syscall.h"

static void syscall_handler (struct intr_frame *);
static void validate_user_string (char* user_str);
static void validate_user_buffer (void* user_buf, unsigned size);
static void* uaddr_to_kaddr (const void* uaddr);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  
}

static void NO_RETURN
syscall_halt(void) {
  shutdown_power_off();
}

static void NO_RETURN
syscall_exit (int status) {
  process_exit_with_value(status);
  NOT_REACHED();
}

static pid_t
syscall_exec(const char *cmd_line) {
#define EXEC_ERROR ((pid_t) -1)
  // we are allowed to impose a reasonable limit on argument data
  // the documentations mentions one page
  if (strlen(cmd_line) > PGSIZE - 1) // -1 null byte
  {
    return EXEC_ERROR;
  }
  
  
  pid_t pid = process_execute(cmd_line);

  if (pid == PID_ERROR)
  {
    return EXEC_ERROR;
  }
  
  // child started with pid
  return pid;
}

static int 
syscall_wait (pid_t pid) {
  return process_wait(pid);
}

static bool 
syscall_create (const char *file, unsigned initial_size) {
  if (strlen(file)>NAME_MAX) return false;

  bool res = filesys_create(file, initial_size);

  return res;
}

static bool 
syscall_remove (const char *file) {
  if (strlen(file)>NAME_MAX) return false;

  bool res = filesys_remove(file);

  return res;
}

static int 
syscall_open (const char *file) {
  if (strlen(file)>NAME_MAX) return false;

  struct file *f = filesys_open(file);

  if (f  == NULL)
    return -1;
  int fd = insert_fdlist(thread_current()->pid, f);
  return fd;
}

static int 
syscall_filesize (int fd) {
  struct file *f = get_fdlist(thread_current()->pid, fd);
  if (!f) // file does not exist
    return -1;

  int res = file_length(f);

  return res;
}

static int 
syscall_read (int fd, void *buffer, unsigned size) {
  if (fd == 0)
  {
    unsigned i;
    char *out = buffer;
    for(i = 0;i < size; i++, out++)
    {
      *out = input_getc();
    }
    return i;
  } else {
    struct file *f = get_fdlist(thread_current()->pid, fd);
    if (!f) // file does not exist
      return -1;

    int res = file_read(f, buffer, size);

    return res;
  }
}

static int 
syscall_write (int fd, const void *buffer, unsigned size) {
  if (fd == 1)
  {
    // TODO long buffer could be split (multiple megabytes)
    putbuf(buffer, size);
    return size;
  } else
  {
    struct file *f = get_fdlist(thread_current()->pid, fd);
    if (!f) // file does not exist
      return 0;

    int res = file_write(f, buffer, size);

    return res;
  }
}

static void 
syscall_seek (int fd, unsigned position) {
  struct file *f = get_fdlist(thread_current()->pid, fd);
  if (!f) // file does not exist
    return;

  file_seek(f, position);

}

static unsigned 
syscall_tell (int fd) {
  struct file *f = get_fdlist(thread_current()->pid, fd);
  if (!f) // file does not exist
    return 0;

  unsigned res = file_tell(f);

  return res;
}

static void 
syscall_close (int fd) {
  struct file *f = get_fdlist(thread_current()->pid, fd);
  if (!f) // file does not exist
    return;
  delete_fdlist(thread_current()->pid, fd);

  file_close(f);

}

/*
 * Validates that every byte of a user provided char* is
 * inside the user's mapped memory.
 * Terminates the user process on address violations.
 *
 * Takes a char* into userspace to validate.
 */
static void
validate_user_string (char* user_str)
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
validate_user_buffer (void* user_buf, unsigned size)
{
  // validate original pointer
  uaddr_to_kaddr(user_buf);
  void* user = user_buf;
  // bytes remaining in page
  unsigned remaining_bytes = PGSIZE - pg_ofs(user);
  if (remaining_bytes < size)
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
  void *buffer_user, *buffer_kernel;
  char *file_name, *file_name_uaddr, *exec_name, *exec_name_uaddr ;
  unsigned size, position;
  int status, pid, fd;

  uint32_t syscall_nr = *((uint32_t*) uaddr_to_kaddr(f->esp));
  printf("SysCall_NR.: %i\n" ,syscall_nr);
  switch (syscall_nr) {
    case SYS_HALT:
                   syscall_halt();
                   break;                  /* Halt the operating system. */
    case SYS_EXIT:
                   status = *((int*) uaddr_to_kaddr(f->esp+4));
                   syscall_exit(status);
                   break;                  /* Terminate this process. */
    case SYS_EXEC:
                   exec_name_uaddr = *((char**) uaddr_to_kaddr(f->esp+4)); /* char pointer in usermode */ 
                   validate_user_string(exec_name_uaddr);
                   exec_name = (char*) uaddr_to_kaddr(exec_name_uaddr); /* char pointer in kernel mode */
                   f->eax = syscall_exec(exec_name);
                   break; /* Start another process. */
    case SYS_WAIT:
                   pid = *((int*) uaddr_to_kaddr(f->esp+4));
                   f->eax = syscall_wait(pid);  /* Wait for a child process to die. */
                   break;
    case SYS_CREATE: 
                   file_name_uaddr = *((char**) uaddr_to_kaddr(f->esp+4)); /* char pointer in usermode */ 
                   validate_user_string(file_name_uaddr);
                   file_name = (char*) uaddr_to_kaddr(file_name_uaddr); /* char pointer in kernel mode */
                   size = *((unsigned*) uaddr_to_kaddr(f->esp+8));
                   f->eax = syscall_create(file_name, size);
                   break;                /* Create a file. */
    case SYS_REMOVE:
                   file_name_uaddr = *((char**) uaddr_to_kaddr(f->esp+4)); /* char pointer in usermode */ 
                   validate_user_string(file_name_uaddr);
                   file_name = (char*) uaddr_to_kaddr(file_name_uaddr); /* char pointer in kernel mode */
                   f->eax = syscall_remove(file_name);
                   break;                 /* Delete a file. */
    case SYS_OPEN:
                   file_name_uaddr = *((char**) uaddr_to_kaddr(f->esp+4)); /* char pointer in usermode */ 
                   validate_user_string(file_name_uaddr);
                   file_name = (char*) uaddr_to_kaddr(file_name_uaddr); /* char pointer in kernel mode */
                   f->eax = syscall_open(file_name);
                   break;                  /* Open a file. */
    case SYS_FILESIZE: 
                   fd = *((int*) uaddr_to_kaddr(f->esp+4));
                   f->eax = syscall_filesize(fd);
                   break;              /* Obtain a file's size. */
    case SYS_READ:
                   fd = *((int*) uaddr_to_kaddr(f->esp+4));
                   buffer_user = *((void**)uaddr_to_kaddr(f->esp+8)); /* void* in user mode */
                   size = *((unsigned *)uaddr_to_kaddr(f->esp+12));
                   validate_user_buffer(buffer_user, size); /* validates user input */
                   printf("4\n");
                   buffer_kernel = uaddr_to_kaddr(buffer_user); /* void* in kernel mode */
                   printf("5\n");
                   f->eax = syscall_read(fd, buffer_kernel, size);
                   
                   break;    /* Read from a file. */
    case SYS_WRITE:
                   fd = *((int*) uaddr_to_kaddr(f->esp+4));
                   buffer_user = *((void**)uaddr_to_kaddr(f->esp+8)); /* void* in user mode */
                   size = *((unsigned *)uaddr_to_kaddr(f->esp+12));
                   validate_user_buffer(buffer_user, size); /* validates user input */
                   buffer_kernel = uaddr_to_kaddr(buffer_user); /* void* in kernel mode */
                   f->eax = syscall_write(fd, buffer_kernel, size);
                   break;                  /* Write to a file. */
    case SYS_SEEK:
                   fd = *((int*) uaddr_to_kaddr(f->esp+4));
                   position = *((unsigned*) uaddr_to_kaddr(f->esp+8));
                   syscall_seek(fd,position); 
                   break;  /* Change position in a file. */
    case SYS_TELL: 
                   fd = *((int*) uaddr_to_kaddr(f->esp+4));
                   f->eax = syscall_tell(fd);
                   break;                  /* Report current position in a file. */
    case SYS_CLOSE:
                   fd = *((int*) uaddr_to_kaddr(f->esp+4));
                   syscall_close(fd); 
                   break;                  /* Close a file. */
    default:
                   syscall_exit(-1);
                   break; /* Should not happen */ 
  }
}

static void* 
uaddr_to_kaddr (const void* uaddr) {
  // check for null pointers

  if (!uaddr) {
      syscall_exit(-1); /* address violation */
      NOT_REACHED ();
  }
  // TODO Lock needed?
  if (is_user_vaddr(uaddr)){
    if (pagedir_is_assigned(thread_current()->pagedir, uaddr)) {
      void* page = pagedir_get_page(thread_current()->pagedir, uaddr);
      if (page) {
        frame_set_pin(page, true);
        return page;
      }
      else {
        if (spage_valid_and_load(uaddr, true)) {
          return pagedir_get_page(thread_current()->pagedir, uaddr); 
       }
    }   
    }
    else {
       
      syscall_exit(-1); /* address violation */
      NOT_REACHED ();
    }
  }
  else {
    syscall_exit(-1); /* address violation */
    NOT_REACHED ();
  }
}