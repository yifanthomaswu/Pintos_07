#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include "threads/thread.h"

/* Lock used to synchronise any access to the file system. */
extern struct lock file_lock;

struct list statuses;
struct list mapids;
typedef int mapid_t;

/* Struct used to map a process tid to related semaphores. */
struct process_sema
{
  struct list_elem process_semaelem;
  tid_t tid;
  /* Inform parent whether the child process is successfully created. */
  bool load_fail;
  /* Used to synchronise parent and child during exec system call. */
  struct semaphore sema_exec;
  /* Used to synchronise parent and child during wait system call. */
  struct semaphore sema_wait;
};

void syscall_init (void);
void *syscall_user_memory (const void *vaddr, bool write);
void pre_exit (int status);

struct process_sema *add_process_sema (tid_t tid);
struct process_sema *get_process_sema (tid_t tid);
void remove_process_sema (tid_t tid);
void add_status (tid_t tid);
void set_status (tid_t tid, int status);
void remove_status(tid_t tid);
int get_exit_code (tid_t tid);
bool is_waited_on (tid_t tid);
bool is_dead (tid_t tid);

static mapid_t mmap (int fd, void *addr);
void munmap (mapid_t mapping);

#endif /* userprog/syscall.h */
