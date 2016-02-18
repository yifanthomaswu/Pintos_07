#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include "threads/thread.h"

#define KILLED_EXIT_CODE -1

void syscall_init (void);
int get_exit_code (tid_t pid);
void set_exit_code (tid_t pid, int status);
bool is_waited_on (tid_t pid);
void set_waited_on (tid_t pid, bool waited_on);
bool is_dead (tid_t pid);

#endif /* userprog/syscall.h */
