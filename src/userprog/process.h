#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

typedef int pid_t;

void process_init (void);

pid_t process_execute (const char *file_name);
int process_wait (pid_t);
void process_exit (void);
void process_exit_with_value (int exit_value);
void process_activate (void);

#endif /* userprog/process.h */
