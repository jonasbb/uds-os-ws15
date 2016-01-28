#include "filesys/file.h"
#include <debug.h>
#include <string.h>
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"
#include "filesys/file-struct.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Opens a file for the given INODE, of which it takes ownership,
   and returns the new file.  Returns a null pointer if an
   allocation fails or if INODE is null. */
struct file *
file_open (struct inode *inode)
{

  struct file *file = calloc (1, sizeof *file);
  struct file *res;
  if (inode != NULL && file != NULL)
    {
      file->inode = inode;
      file->pos = 0;
      file->deny_write = false;
      res = file;
    }
  else
    {
      inode_close (inode);
      free (file);
      res = NULL; 
    }

  return res;
}

/* Opens and returns a new file for the same inode as FILE.
   Returns a null pointer if unsuccessful. */
struct file *
file_reopen (struct file *file) 
{
  return file_open (inode_reopen (file->inode));
}

/* Closes FILE. */
void
file_close (struct file *file) 
{

  if (file != NULL)
    {
      file_allow_write (file);
      inode_close (file->inode);
      free (file); 
    }

}

/* Returns the inode encapsulated by FILE. */
struct inode *
file_get_inode (struct file *file) 
{
  return file->inode;
}

/* Reads SIZE bytes from FILE into BUFFER,
   starting at the file's current position.
   Returns the number of bytes actually read,
   which may be less than SIZE if end of file is reached.
   Advances FILE's position by the number of bytes read. */
off_t
file_read (struct file *file, void *buffer, off_t size) 
{

  off_t bytes_read = inode_read_at (file->inode, buffer, size, file->pos);
  file->pos += bytes_read;

  return bytes_read;
}

/* Reads SIZE bytes from FILE into BUFFER,
   starting at offset FILE_OFS in the file.
   Returns the number of bytes actually read,
   which may be less than SIZE if end of file is reached.
   The file's current position is unaffected. */
off_t
file_read_at (struct file *file, void *buffer, off_t size, off_t file_ofs) 
{
  off_t bytes_read = inode_read_at (file->inode, buffer, size, file_ofs);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into FILE,
   starting at the file's current position.
   Returns the number of bytes actually written,
   which may be less than SIZE if end of file is reached.
   (Normally we'd grow the file in that case, but file growth is
   not yet implemented.)
   Advances FILE's position by the number of bytes read. */
off_t
file_write (struct file *file, const void *buffer, off_t size) 
{

  off_t bytes_written = inode_write_at (file->inode, buffer, size, file->pos);
  file->pos += bytes_written;

  return bytes_written;
}

/* Writes SIZE bytes from BUFFER into FILE,
   starting at offset FILE_OFS in the file.
   Returns the number of bytes actually written,
   which may be less than SIZE if end of file is reached.
   (Normally we'd grow the file in that case, but file growth is
   not yet implemented.)
   The file's current position is unaffected. */
off_t
file_write_at (struct file *file, const void *buffer, off_t size,
               off_t file_ofs) 
{

  off_t bytes_written = inode_write_at (file->inode, buffer, size, file_ofs);

  return bytes_written;
}

/* Prevents write operations on FILE's underlying inode
   until file_allow_write() is called or FILE is closed. */
void
file_deny_write (struct file *file) 
{

  ASSERT (file != NULL);
  if (!file->deny_write) 
    {
      file->deny_write = true;
      inode_deny_write (file->inode);
    }

}

/* Re-enables write operations on FILE's underlying inode.
   (Writes might still be denied by some other file that has the
   same inode open.) */
void
file_allow_write (struct file *file) 
{

  ASSERT (file != NULL);
  if (file->deny_write)
    {
      file->deny_write = false;
      inode_allow_write (file->inode);
    }

}

/* Returns the size of FILE in bytes. */
off_t
file_length (struct file *file) 
{

  ASSERT (file != NULL);
  off_t res = inode_length (file->inode);

  return res;
}

/* Sets the current position in FILE to NEW_POS bytes from the
   start of the file. */
void
file_seek (struct file *file, off_t new_pos)
{

  ASSERT (file != NULL);
  ASSERT (new_pos >= 0);
  file->pos = new_pos;

}

/* Returns the current position in FILE as a byte offset from the
   start of the file. */
off_t
file_tell (struct file *file) 
{

  ASSERT (file != NULL);
  off_t res = file->pos;

  return res;
}

int file_get_inumber(struct file *file) {
  ASSERT(file != NULL);
  return inode_get_inumber(file->inode);
}

bool
file_isdir (struct file *file) {
  ASSERT(file != NULL);
  return file->parent != NULL;
}

bool
file_isroot (struct file *file) {
  ASSERT(file != NULL);
  return file->parent == file;
}


bool
file_deconstruct_path (const char   *path,
                       struct file **parent,
                       struct file **file,
                       char        *filename[NAME_MAX + 1]) {
  char s[strlen(path) + 1];
  memcpy(s, path, strlen(path) + 1);

  char *save_ptr, *token, *token_next;
  // token_next contains always the last processed part of the string,
  // which may be the non-existant new filename
  // so only token will be checked to go through the directories

  struct file *dir, *f = NULL;
  struct inode *inode;
  if(s[0] == '/' || !thread_current()->current_work_dir) {
    dir = dir_open_root();
  } else {
    dir = dir_reopen(thread_current()->current_work_dir);
  }

  // parse string
  token_next = strtok_r(s, "/", &save_ptr);
  if (!token_next) {
    return false;
  }

  while(token_next != NULL) {
    token = token_next;
    token_next = strtok_r(NULL, "/", &save_ptr);

    if (strcmp(token, ".") == 0) {
      continue;
    }
    if (strcmp(token, "..") == 0) {
      dir = dir_pop(dir);
      continue;
    }

    if (!dir_lookup(dir, token, &inode)) {
      break;
    }

    dir = dir_open_with_parent(inode, dir);
  }

  if (strtok_r(NULL, "/", &save_ptr) != NULL) {
    // not all tokens processed, there is an error
    dir_close(dir);
    return false;
  }

  if (file) {
    if (dir_lookup(dir, token, &inode)) {
      f = file_open(inode);
    }
  }

  ASSERT(file_isdir(dir));
  // set return values
  if (parent) {
    *parent = dir;
  } else {
    dir_close(dir);
  }
  if (file) {
    *file = f;
  } else {
    if (f) {
      file_close(f);
    }
  }
  if (filename) {
    memcpy(*filename, token, strlen(token) + 1);
  }

  return true;
}
