#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "filesys/file.h"
#include <stdlib.h>
#include <stdbool.h>

typedef int pid_t;

void process_init (void);

int insert_fdlist(pid_t, struct file* );
bool delete_fdlist(pid_t, int);
struct file* get_fdlist(pid_t, int);
void close_fdlist(int pid);


pid_t process_execute (const char *file_name);
int process_wait (pid_t);
void process_exit (void);
void process_exit_with_value (int exit_value);
void process_activate (void);

#endif /* userprog/process.h */
