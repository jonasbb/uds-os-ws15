#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  printf ("system call!\n");

  if (is_user_vaddr(intr_frame->esp)){
    page = pagedir_get_page(thread_current()->pd, intr_frame->esp);
    if (page) {
      syscall_nr = (intr_frame->esp)*;
    } 
    else {
      exit(-1); /* address violation */
    }
  }
  else {
    exit(-1); /* address violation */
  }
  
  switch (syscall_nr) {
    case SYS_HALT: halt(); break;                  /* Halt the operating system. */
    case SYS_EXIT: exit(status); break;                  /* Terminate this process. */
    case SYS_EXEC: intr_frame->eax = exec(exec_name); break; /* Start another process. */
    case SYS_WAIT:                   /* Wait for a child process to die. */
    case SYS_CREATE:                 /* Create a file. */
    case SYS_REMOVE:                 /* Delete a file. */
    case SYS_OPEN:                   /* Open a file. */
    case SYS_FILESIZE:               /* Obtain a file's size. */
    case SYS_READ:                   /* Read from a file. */
    case SYS_WRITE:                  /* Write to a file. */
    case SYS_SEEK: seek(fd,position); break;  /* Change position in a file. */
    case SYS_TELL:                   /* Report current position in a file. */
    case SYS_CLOSE: close(fd); break;                  /* Close a file. */
    default: thread_exit (); break; /* Should not happen */ 
  }
    
  
  
}
