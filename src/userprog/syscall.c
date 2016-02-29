#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

/* Struct used to keep the history of dead processes and their exit codes. */
struct status
  {
    struct list_elem statuselem;
    tid_t tid;
    int status;
    /* A process can be waited on only once. */
    bool waited_on;
  };

/* Lock used to synchronise any access to the file system. */
struct lock file_lock;
/* The current free file descriptor available to a process. */
/* Accessible through the get_new_fd() function. */
static int fd_count;
/* History of dead processes. */
static struct list statuses;
/* List of waiting processes on their children to die. */
static struct list processes;

static struct semaphore fd_count_sema;
static struct semaphore statuses_sema;
static struct semaphore processes_sema;

static void syscall_handler (struct intr_frame *);
static inline int get_new_fd (void);
static struct file_fd *get_file_fd (int fd);

static void halt (void);
static void exit (int status);
tid_t exec (const char *cmd_line);
static int wait (tid_t tid);
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static int filesize (int fd);
static int read (int fd, void *buffer, unsigned size);
static int write (int fd, const void *buffer, unsigned size);
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static int open (const char *file);
static void close (int fd);

static struct status *get_status (tid_t tid);

void
syscall_init (void)
{
  /* Initialise the next available fd to 2; 0 and 1 reserved for STD[IN/OUT]. */
  fd_count = 2;
  /* Initialise the file system lock. */
  lock_init (&file_lock);
  /* Initialise the used lists. */
  list_init (&statuses);
  list_init (&processes);

  sema_init (&fd_count_sema, 1);
  sema_init (&statuses_sema, 1);
  sema_init (&processes_sema, 1);
  /* Register the system call handler on 0x30. */
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  uint32_t *sp = f->esp;
  if (syscall_user_memory (sp) == NULL)
    exit (-1);
  uint32_t arg0 = 0, arg1 = 0, arg2 = 0;
  switch (*sp)
    {
    case SYS_READ:
    case SYS_WRITE:
      if (syscall_user_memory (sp + 3) == NULL)
        exit (-1);
      else
        arg2 = *(sp + 3);
    case SYS_CREATE:
    case SYS_SEEK:
      if (syscall_user_memory (sp + 2) == NULL)
        exit (-1);
      else
        arg1 = *(sp + 2);
    case SYS_EXIT:
    case SYS_EXEC:
    case SYS_WAIT:
    case SYS_REMOVE:
    case SYS_OPEN:
    case SYS_FILESIZE:
    case SYS_TELL:
    case SYS_CLOSE:
      if (syscall_user_memory (sp + 1) == NULL)
        exit (-1);
      else
        arg0 = *(sp + 1);
    }
  switch (*sp)
    {
    case SYS_HALT:                   /* Halt the operating system. */
      halt ();
      break;
    case SYS_EXIT:                   /* Terminate this process. */
      f->eax = arg0;
      exit (arg0);
      break;
    case SYS_EXEC:                   /* Start another process. */
      f->eax = exec ((char *) arg0);
      break;
    case SYS_WAIT:                   /* Wait for a child process to die. */
      f->eax = wait (arg0);
      break;
    case SYS_CREATE:                 /* Create a file. */
      f->eax = create ((char *) arg0, (unsigned) arg1);
      break;
    case SYS_REMOVE:                 /* Delete a file. */
      f->eax = remove ((char *) arg0);
      break;
    case SYS_OPEN:                   /* Open a file. */
      f->eax = open ((char *) arg0);
      break;
    case SYS_FILESIZE:               /* Obtain a file's size. */
      f->eax = filesize (arg0);
      break;
    case SYS_READ:                   /* Read from a file. */
      f->eax = read (arg0, (void *) arg1, (unsigned) arg2);
      break;
    case SYS_WRITE:                  /* Write to a file. */
      f->eax = write (arg0, (void *) arg1, (unsigned) arg2);
      break;
    case SYS_SEEK:                   /* Change position in a file. */
      seek (arg0, (unsigned) arg1);
      break;
    case SYS_TELL:                   /* Report current position in a file. */
      f->eax = tell (arg0);
      break;
    case SYS_CLOSE:                  /* Close a file. */
      close (arg0);
      break;
    }
}

void *
syscall_user_memory (const void *vaddr)
{
  if (is_user_vaddr (vaddr))
    return pagedir_get_page (thread_current ()->pagedir, vaddr);
  else
    return NULL;
}

/* Returns and then increments the next available file descriptor. */
static inline int
get_new_fd (void)
{
  return fd_count++;
}

/* Returns file pointer to the file referenced by fd. */
/* If no file is open through fd, returns NULL. */
static struct file_fd *
get_file_fd (int fd)
{
  struct thread *t = thread_current();
  struct list_elem *e;
  for (e = list_begin (&t->files); e != list_end (&t->files);
       e = list_next (e))
    {
      struct file_fd *f = list_entry (e, struct file_fd, filefdelem);
      if (f->fd == fd)
        return f;
    }
  return NULL;
}

static void
halt (void)
{
  shutdown_power_off ();
}

static void
exit (int status)
{
  pre_exit (status);
  thread_exit ();
}

void
pre_exit (int status)
{
  struct thread *t = thread_current ();
  /* An exception has exit code -1, add it to history of processes. */
  add_status (t->tid, status);

  /* Get the semaphore struct of the process, if not null up the wait semaphore. */
  struct process_sema *p_s = get_process_sema (t->parent_tid);
  if (p_s != NULL)
    sema_up (&p_s->sema_wait);

  /* Get the processes executable file, if exists, close it. */
  struct file * exec_file = t->exec_file;
  if (exec_file != NULL)
    {
      lock_acquire (&file_lock);
      file_close (exec_file);
      lock_release (&file_lock);
    }
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
}

tid_t
exec (const char *cmd_line)
{
  if (syscall_user_memory (cmd_line) == NULL)
    exit (-1);
  tid_t new_tid = process_execute (cmd_line);
  if (new_tid != -1)
    {
      struct thread *t = thread_current ();
      struct process_sema *p_s = add_process_sema (t->tid);
      sema_down (&p_s->sema_exec);
      if (p_s->load_fail)
        {
          struct list_elem *e;
          for (e = list_begin (&t->children); e != list_end (&t->children);
               e = list_next (e))
            {
              struct child_tid *c_t = list_entry (e, struct child_tid,
                                                  childtidelem);
              if (c_t->tid == new_tid)
                {
                  list_remove (e);
                  free (c_t);
                  break;
                }
            }
          return -1;
        }
    }
  return new_tid;
}

static int
wait (tid_t tid)
{
  if (!is_child (tid) || is_waited_on (tid))
    return -1;

  if (is_dead (tid))
    {
      set_waited_on (tid);
      return get_exit_code (tid);
    }
  return process_wait (tid);
}

static bool
create (const char *file, unsigned initial_size)
{
  if (syscall_user_memory (file) == NULL)
    exit (-1);
  lock_acquire (&file_lock);
  bool success = filesys_create (file, initial_size);
  lock_release (&file_lock);
  return success;
}

static bool
remove (const char *file)
{
  if (syscall_user_memory (file) == NULL)
    exit (-1);
  lock_acquire (&file_lock);
  bool success = filesys_remove (file);
  lock_release (&file_lock);
  return success;
}

static int
open (const char *file)
{
  if (syscall_user_memory (file) == NULL)
    exit (-1);
  lock_acquire (&file_lock);
  struct file *current_file = filesys_open (file);
  lock_release (&file_lock);
  if (current_file == NULL)
    return -1;
  else
    {
      struct file_fd *file_fd = malloc (sizeof(struct file_fd));
      if (file_fd == NULL)
        return -1;
      sema_down (&fd_count_sema);
      file_fd->fd = get_new_fd ();
      sema_up (&fd_count_sema);
      int length = strlen (file) + 1;
      file_fd->file_name = malloc (length * sizeof(char));
      if (file_fd->file_name == NULL)
        {
          free (file_fd);
          return -1;
        }
      memcpy (file_fd->file_name, file, length);
      file_fd->file = current_file;
      list_push_front (&thread_current ()->files, &file_fd->filefdelem);
      return file_fd->fd;
    }
}

static int
filesize (int fd)
{
  if (fd == STDIN_FILENO || fd == STDOUT_FILENO)
    return 0;
  else
    {
      struct file_fd *file_fd = get_file_fd (fd);
      if (file_fd == NULL)
        return 0;
      else
        {
          lock_acquire (&file_lock);
          int s = file_length (file_fd->file);
          lock_release (&file_lock);
          return s;
        }
    }
}

static int
read (int fd, void *buffer, unsigned size)
{
  if (syscall_user_memory (buffer) == NULL)
    exit (-1);
  if (fd == STDIN_FILENO)
    {
      uint8_t *char_buffer = buffer;
      int i;
      for (i = 0; i < (int) size; i++)
        {
          *char_buffer = input_getc ();
          char_buffer++;
        }
      *char_buffer = '\0';
      return size;
    }
  else if (fd == STDOUT_FILENO)
    return -1;
  else
    {
      struct file_fd *file_fd = get_file_fd (fd);
      if (file_fd == NULL)
        return -1;
      else
        {
          lock_acquire (&file_lock);
          int s = file_read (file_fd->file, buffer, size);
          lock_release (&file_lock);
          return s;
        }
    }
}

static int
write (int fd, const void *buffer, unsigned size)
{
  if (syscall_user_memory (buffer) == NULL)
    exit (-1);
  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      return size;
    }
  else if (fd == STDIN_FILENO)
    return 0;
  else
    {
      struct file_fd *file_fd = get_file_fd (fd);
      if (file_fd == NULL)
        return 0;
      else
        {
          lock_acquire (&file_lock);
          int s = file_write (file_fd->file, buffer, size);
          lock_release (&file_lock);
          return s;
        }
    }
}

static void
seek (int fd, unsigned position)
{
  struct file_fd *file_fd = get_file_fd (fd);
  if (file_fd != NULL)
    {
      lock_acquire (&file_lock);
      file_seek (file_fd->file, (int32_t) position);
      lock_release (&file_lock);
    }
}

static unsigned
tell (int fd)
{
  struct file_fd *file_fd = get_file_fd (fd);
  lock_acquire (&file_lock);
  unsigned p = file_tell (file_fd->file);
  lock_release (&file_lock);
  return p;
}

static void
close (int fd)
{
  struct file_fd *file_fd = get_file_fd (fd);
  if (file_fd != NULL)
    {
      lock_acquire (&file_lock);
      file_close (file_fd->file);
      lock_release (&file_lock);
      list_remove (&file_fd->filefdelem);
      free (file_fd->file_name);
      free (file_fd);
    }
}

struct process_sema *
add_process_sema (tid_t tid)
{
  struct process_sema *p_s = get_process_sema (tid);
  if (p_s == NULL)
    {
      p_s = malloc (sizeof(struct process_sema));
      if (p_s == NULL)
        exit (-1);
      p_s->tid = tid;
      sema_init (&p_s->sema_wait, 0);
      sema_init (&p_s->sema_exec, 0);
      sema_down (&processes_sema);
      list_push_front (&processes, &p_s->process_semaelem);
      sema_up (&processes_sema);
      return p_s;
    }
  else
    return p_s;
}

struct process_sema *
get_process_sema (tid_t tid)
{
  struct list_elem *e;
  sema_down (&processes_sema);
  for (e = list_begin (&processes); e != list_end (&processes);
       e = list_next (e))
    {
      struct process_sema *p_s = list_entry (e, struct process_sema,
                                             process_semaelem);
      if (p_s->tid == tid)
        {
          sema_up (&processes_sema);
          return p_s;
        }
    }
  sema_up (&processes_sema);
  return NULL;
}

void
remove_process_sema (tid_t tid)
{
  struct process_sema *p_s = get_process_sema (tid);
  if (p_s != NULL)
    {
      list_remove (&p_s->process_semaelem);
      free (p_s);
    }
}

void
add_status (tid_t tid, int status)
{
  struct status *new_status = malloc (sizeof(struct status));
  if (new_status == NULL)
    exit (-1);
  new_status->tid = tid;
  new_status->waited_on = false;
  new_status->status = status;
  sema_down (&statuses_sema);
  list_push_front (&statuses, &new_status->statuselem);
  sema_up (&statuses_sema);
}

static struct status *
get_status (tid_t tid)
{
  struct list_elem *e;
  sema_down (&statuses_sema);
  for (e = list_begin (&statuses); e != list_end (&statuses);
       e = list_next (e))
    {
      struct status *s = list_entry (e, struct status, statuselem);
      if (s->tid == tid)
        {
          sema_up (&statuses_sema);
          return s;
        }
    }
  sema_up (&statuses_sema);
  return NULL;
}

int
get_exit_code (tid_t tid)
{
  struct status *s = get_status (tid);
  if (s != NULL)
    return s->status;
  NOT_REACHED ();
}

bool
is_waited_on (tid_t tid)
{
  struct status *s = get_status (tid);
  if (s != NULL)
    return s->waited_on;
  else
    return false;
}

void
set_waited_on (tid_t tid)
{
  struct status *s = get_status (tid);
  if (s != NULL)
    s->waited_on = true;
}

bool
is_dead (tid_t tid)
{
  return get_status (tid) != NULL;
}
