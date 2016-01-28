#include <stdio.h>
#include <debug.h>
#include <stdbool.h>
#include <string.h>
#include <syscall-nr.h>
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/directory.h"
#include "filesys/inode.h"
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
#include "lib/round.h"
#include "vm/spage.h"
#include "vm/frames.h"

static void syscall_handler (struct intr_frame *);
static unsigned validate_user_string (char* user_str, void *esp);
static void validate_user_buffer (void* user_buf, int size, void *esp);
static void* uaddr_to_kaddr (const void* uaddr, void *esp);
static void* uaddr_to_kaddr_write (const void* uaddr, bool write, void *esp);
static void unpin_page (void* uaddr);
static void unpin_buffer (void* uaddr, int size);
typedef int mapid_t;

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

static void
syscall_munmap(mapid_t mapid) {
    pid_t pid = thread_current()->pid;
    delete_mmaplist(pid, mapid);
}

static mapid_t
syscall_mmap (int fd, void *vaddr) {
  struct file *f = get_fdlist(thread_current()->pid, fd);
  if (!f) // file does not exist
    return -1;
  if (vaddr == NULL)
    return -1;
  if (pg_ofs(vaddr) != 0)
    return -1;

  size_t fsize = file_length(f);
  if (fsize == 0)
    return -1;
  size_t pgcount = DIV_ROUND_UP(fsize, PGSIZE);

  // check memory range for overlaps with already existing mappings
  size_t i, s;
  for(i = 0; i < pgcount; i++) {
    if (pagedir_is_assigned(thread_current()->pagedir, vaddr + i * PGSIZE))
      return -1;
  }

  struct file *f_ = file_reopen(f);
  if (!f_)
    return -1;
  pid_t pid = thread_current()->pid;
  mapid_t mapid = insert_mmaplist(pid, vaddr, f_);
  for(i = 0, s = fsize; i < pgcount; i++, s -= PGSIZE) {
    if (spage_map_mmap(f_,
                       i * PGSIZE,
                       vaddr + i * PGSIZE,
                       true,
                       s > PGSIZE ? PGSIZE : s)) {
      inc_pgcount_mmaplist(pid, mapid);
    } else {
      // mapping failed, undo everything
      syscall_munmap(mapid);
      return -1;
    }
  }
  return mapid;
}

static bool
syscall_chdir(const char* file_name) {
  //TODO
  return false;
}

static bool
syscall_mkdir(const char* file_name) {
  //TODO
  return false;
}

static bool
syscall_isdir(int fd) {
  struct file *f = get_fdlist(thread_current()->pid, fd);
  if (!f) // file does not exist
    return false;
  return file_isdir(f);
}

static int
syscall_inumber(int fd) {
  struct file *f = get_fdlist(thread_current()->pid, fd);
  if (!f) // file does not exist
    return -1;
  return file_get_inumber(f);
}

static bool
syscall_readdir(int fd, const char * file_name) {
  //TODO
  return false;
}


/*
 * Validates that every byte of a user provided char* is
 * inside the user's mapped memory.
 * Terminates the user process on address violations.
 *
 * Takes a char* into userspace to validate. Returns the size of the string.
 */
static unsigned
validate_user_string (char* user_str, void *esp)
{
  // validate original pointer
  char* kernel = uaddr_to_kaddr(user_str, esp);
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
      kernel = uaddr_to_kaddr(user, esp);
      current_page = pg_no(user);
    }
  }
  return (unsigned) (user - user_str + 1);
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
validate_user_buffer_write (void* user_buf, int size, void *esp, bool write)
{
  ASSERT(size >= 0);
  // validate original pointer
  uaddr_to_kaddr_write(user_buf,write , esp);
  void* user = user_buf;
  // bytes remaining in page
  int remaining_bytes = PGSIZE - pg_ofs(user);
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
  do {
    // validate pointer, esp
    uaddr_to_kaddr_write(user,write, esp);
    // move pointer to next page
    size -= PGSIZE;
    user += PGSIZE;
  } while (size > 0);
}

static void
validate_user_buffer (void* user_buf, int size, void *esp)
{
  validate_user_buffer_write(user_buf, size, esp, false);
}

static void
syscall_handler (struct intr_frame *f) 
{
  void *buffer_user;
  char *file_name, *file_name_uaddr, *exec_name, *exec_name_uaddr ;
  unsigned size, position, s_l;
  int status, pid, fd, mapid;
  void *vaddr;

  void *esp =f->esp;
  uint32_t syscall_nr = *((uint32_t*) uaddr_to_kaddr(f->esp, esp));
  switch (syscall_nr) {
    case SYS_HALT:
                   log_debug("SYS_HALT\n");
                   syscall_halt();
                   break;                  /* Halt the operating system. */
    case SYS_EXIT:
                   log_debug("SYS_EXIT\n");
                   status = *((int*) uaddr_to_kaddr(f->esp+4, esp));
                   syscall_exit(status);
                   unpin_page(f->esp+4);
                   break;                  /* Terminate this process. */
    case SYS_EXEC:
                   log_debug("SYS_EXEC\n");
                   exec_name_uaddr = *((char**) uaddr_to_kaddr(f->esp+4, esp)); /* char pointer in usermode */
                   s_l = validate_user_string(exec_name_uaddr, esp);
                   exec_name = (char*) uaddr_to_kaddr(exec_name_uaddr, esp); /* char pointer in kernel mode */
                   f->eax = syscall_exec(exec_name);
                   unpin_page(f->esp+4);
                   unpin_buffer(f->esp+4, s_l);
                   break; /* Start another process. */
    case SYS_WAIT:
                   log_debug("SYS_WAIT\n");
                   pid = *((int*) uaddr_to_kaddr(f->esp+4, esp));
                   f->eax = syscall_wait(pid);  /* Wait for a child process to die. */
                   unpin_page(f->esp+4);
                   break;
    case SYS_CREATE: 
                   log_debug("SYS_CREATE\n");
                   file_name_uaddr = *((char**) uaddr_to_kaddr(f->esp+4, esp)); /* char pointer in usermode */
                   s_l = validate_user_string(file_name_uaddr, esp);
                   file_name = (char*) uaddr_to_kaddr(file_name_uaddr, esp); /* char pointer in kernel mode */
                   size = *((unsigned*) uaddr_to_kaddr(f->esp+8, esp));
                   f->eax = syscall_create(file_name, size);
                   unpin_page(f->esp+4);
                   unpin_page(f->esp+8);
                   unpin_buffer(f->esp+4, s_l);
                   break;                /* Create a file. */
    case SYS_REMOVE:
                   log_debug("SYS_REMOVE\n");
                   file_name_uaddr = *((char**) uaddr_to_kaddr(f->esp+4, esp)); /* char pointer in usermode */
                   s_l = validate_user_string(file_name_uaddr, esp);
                   file_name = (char*) uaddr_to_kaddr(file_name_uaddr, esp); /* char pointer in kernel mode */
                   f->eax = syscall_remove(file_name);
                   unpin_page(f->esp+4);
                   unpin_buffer(f->esp+4, s_l);
                   break;                 /* Delete a file. */
    case SYS_OPEN:
                   log_debug("SYS_OPEN\n");
                   file_name_uaddr = *((char**) uaddr_to_kaddr(f->esp+4, esp)); /* char pointer in usermode */
                   s_l = validate_user_string(file_name_uaddr, esp);
                   file_name = (char*) uaddr_to_kaddr(file_name_uaddr, esp); /* char pointer in kernel mode */
                   f->eax = syscall_open(file_name);
                   unpin_page(f->esp+4);
                   unpin_buffer(f->esp+4, s_l);
                   break;                  /* Open a file. */
    case SYS_FILESIZE:
                   log_debug("SYS_FILESIZE\n");
                   fd = *((int*) uaddr_to_kaddr(f->esp+4, esp));
                   f->eax = syscall_filesize(fd);
                   break;              /* Obtain a file's size. */
    case SYS_READ:
                   log_debug("SYS_READ\n");
                   fd = *((int*) uaddr_to_kaddr(f->esp+4, esp));
                   buffer_user = *((void**)uaddr_to_kaddr(f->esp+8 ,esp)); /* void* in user mode */
                   size = *((unsigned *)uaddr_to_kaddr(f->esp+12, esp));
                   validate_user_buffer_write(buffer_user, size, esp, true); /* validates user input */                    
                   f->eax = syscall_read(fd, buffer_user, size);
                   // unpinning
                   unpin_page(f->esp+4);
                   unpin_page(f->esp+8);
                   unpin_page(f->esp+12);
                   unpin_buffer(buffer_user, size);
                   break;    /* Read from a file. */
    case SYS_WRITE:
                   log_debug("SYS_WRITE\n");
                   fd = *((int*) uaddr_to_kaddr(f->esp+4, esp));
                   buffer_user = *((void**)uaddr_to_kaddr(f->esp+8, esp)); /* void* in user mode */
                   size = *((unsigned *)uaddr_to_kaddr(f->esp+12, esp));
                   validate_user_buffer(buffer_user, size, esp); /* validates user input */
                   f->eax = syscall_write(fd, buffer_user, size);
                   unpin_page(f->esp+4);
                   unpin_page(f->esp+8);
                   unpin_page(f->esp+12);
                   unpin_buffer(buffer_user, size);
                   break;                  /* Write to a file. */
    case SYS_SEEK:
                   log_debug("SYS_SEEK\n");
                   fd = *((int*) uaddr_to_kaddr(f->esp+4, esp));
                   position = *((unsigned*) uaddr_to_kaddr(f->esp+8, esp));
                   syscall_seek(fd,position); 
                   unpin_page(f->esp+4);
                   unpin_page(f->esp+8);
                   break;  /* Change position in a file. */
    case SYS_TELL: 
                   log_debug("SYS_TELL\n");
                   fd = *((int*) uaddr_to_kaddr(f->esp+4, esp));
                   f->eax = syscall_tell(fd);
                   unpin_page(f->esp+4);
                   break;                  /* Report current position in a file. */
    case SYS_CLOSE:
                   log_debug("SYS_CLOSE\n");
                   fd = *((int*) uaddr_to_kaddr(f->esp+4, esp));
                   syscall_close(fd); 
                   unpin_page(f->esp+4);
                   break;                  /* Close a file. */
    case SYS_MMAP:
                   log_debug("SYS_MMAP\n");
                   fd = *((int*) uaddr_to_kaddr(f->esp+4, esp));
                   vaddr = *((void**) uaddr_to_kaddr(f->esp+8, esp));
                   f->eax = syscall_mmap(fd,vaddr);
                   unpin_page(f->esp+4);
                   unpin_page(f->esp+8);
                   break;
    case SYS_MUNMAP:
                   log_debug("SYS_MUNMAP\n");
                   mapid = *((int*) uaddr_to_kaddr(f->esp+4, esp));
                   syscall_munmap(mapid);
                   unpin_page(f->esp+4);
                   break;
    case SYS_CHDIR:
                   log_debug("SYS_CHDIR\n");
                   file_name_uaddr = *((char**) uaddr_to_kaddr(f->esp+4, esp)); /* char pointer in usermode */
                   s_l = validate_user_string(file_name_uaddr, esp);
                   file_name = (char*) uaddr_to_kaddr(file_name_uaddr, esp); /* char pointer in kernel mode */
                   f->eax = syscall_chdir(file_name);
                   unpin_page(f->esp+4);
                   unpin_buffer(f->esp+4, s_l);
                   break;
    case SYS_MKDIR:
                   log_debug("SYS_MKDIR\n");
                   file_name_uaddr = *((char**) uaddr_to_kaddr(f->esp+4, esp)); /* char pointer in usermode */
                   s_l = validate_user_string(file_name_uaddr, esp);
                   file_name = (char*) uaddr_to_kaddr(file_name_uaddr, esp); /* char pointer in kernel mode */
                   f->eax = syscall_mkdir(file_name);
                   unpin_page(f->esp+4);
                   unpin_buffer(f->esp+4, s_l);
                   break;
    case SYS_READDIR:
                   log_debug("SYS_READDIR\n");
                   fd = *((int*) uaddr_to_kaddr(f->esp+4, esp));
                   file_name_uaddr = *((char**) uaddr_to_kaddr(f->esp+8, esp)); /* char pointer in usermode */
                   s_l = validate_user_string(file_name_uaddr, esp);
                   file_name = (char*) uaddr_to_kaddr(file_name_uaddr, esp); /* char pointer in kernel mode */
                   f->eax = syscall_readdir(fd, file_name);
                   // unpinning
                   unpin_page(f->esp+4);
                   unpin_page(f->esp+8);
                   break;    /* Read from a file. */

    case SYS_ISDIR:
                   log_debug("SYS_ISDIR\n");
                   fd = *((int*) uaddr_to_kaddr(f->esp+4, esp));
                   f->eax = syscall_isdir(fd);
                   unpin_page(f->esp+4);
                   break;
    case SYS_INUMBER:
                   log_debug("SYS_INUMBER\n");
                   fd = *((int*) uaddr_to_kaddr(f->esp+4, esp));
                   f->eax = syscall_inumber(fd);
                   unpin_page(f->esp+4);
                   break;

    default:
                   syscall_exit(-1);
                   break; /* Should not happen */ 
  }
}

static void*
uaddr_to_kaddr (const void* uaddr, void *esp) {
  return uaddr_to_kaddr_write(uaddr, false, esp);
}

static void*
uaddr_to_kaddr_write (const void* uaddr, bool write, void *esp) {
  // check for null pointers
  lock_acquire_re(&vm_lock);
  if (!uaddr) {
      goto error;
  }
  // TODO Lock needed?
  if (is_user_vaddr(uaddr)){
    // done to handle stack growth
    spage_valid_and_load(uaddr, true, esp);
    if (pagedir_is_assigned(thread_current()->pagedir, uaddr)) {
      if (write) {
        if (!pagedir_is_writeable(thread_current()->pagedir, uaddr)) {
            goto error;
        }
      }
      void* page = pagedir_get_page(thread_current()->pagedir, uaddr);
      if (page) {
        frame_set_pin(pg_round_down(page), true);
        lock_release_re(&vm_lock);
        return uaddr;
      }
      else {
        // first spage_valid.. should cover this
        NOT_REACHED(); 
       
      }
    }
    else {
      log_debug("Error\n");
      goto error;
    }
  }

error:
    lock_release_re(&vm_lock);
    syscall_exit(-1); /* address violation */
    NOT_REACHED ();
}

static void
unpin_page (void* uaddr) {
  void* page = pagedir_get_page(thread_current()->pagedir, uaddr);
  frame_set_pin(pg_round_down(page), false);
}

static void
unpin_buffer (void* uaddr, int size) {
  ASSERT(size >= 0);
  void* user = uaddr;
  // bytes remaining in page
  int remaining_bytes = PGSIZE - pg_ofs(user);
  unpin_page(user);
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
  do {
    // unpin page
    unpin_page(user);
    // move pointer to next page
    size -= PGSIZE;
    user += PGSIZE;
  } while (size > 0);
}
