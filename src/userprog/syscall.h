#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include "threads/thread.h"

#define KILLED_EXIT_CODE -1

void syscall_init (void);
void add_process (tid_t tid, int status);
struct semaphore* add_parent (tid_t tid);
struct semaphore* get_parent_semaphore (tid_t parent_tid);
int get_exit_code (tid_t tid);
void set_exit_code (tid_t tid, int status);
bool is_waited_on (tid_t tid);
void set_waited_on (tid_t tid, bool waited_on);
bool is_dead (tid_t tid);

#endif /* userprog/syscall.h */
