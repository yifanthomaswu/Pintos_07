#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include "threads/thread.h"

/* Lock used to synchronise any access to the file system. */
extern struct lock file_lock;

/* Struct used to map a process tid to the semaphore it uses to wait on process. */
struct process_sema
{
  struct list_elem process_semaelem;
  tid_t tid;
  bool load_fail;
  struct semaphore sema_wait;
  struct semaphore sema_exec;
};

void syscall_init (void);
void *syscall_user_memory (const void *vaddr);
void pre_exit (int status);

struct process_sema *add_process_sema (tid_t tid);
struct process_sema *get_process_sema (tid_t tid);
void remove_process_sema (tid_t tid);
void add_status (tid_t tid, int status);
int get_exit_code (tid_t tid);
bool is_waited_on (tid_t tid);
void set_waited_on (tid_t tid);
bool is_dead (tid_t tid);

#endif /* userprog/syscall.h */
