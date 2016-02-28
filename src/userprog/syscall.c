#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "devices/input.h"

/* Struct used to keep the history of dead processes and their exit codes. */
struct exitstatus
  {
    struct list_elem statuselem;
    tid_t tid;
    int status;
    /* A process can be waited on only once. */
    bool waited_on;
  };

/* Struct used to map a process tid to the semaphore it uses to wait on process. */
struct process_sema
{
  struct list_elem process_sema_elem;
  tid_t tid;
  struct semaphore sema;
};

/* History of dead processes. */
struct list statuses;
/* List of waiting processes on their children to die. */
struct list parents;

/* The current free file descriptor available to a process. */
/* Accessible through the get_new_fd() function. */
static int fd;
/* Lock used to synchronise any access to the file system. */
static struct lock file_lock;

static void syscall_handler (struct intr_frame *);
static void *syscall_user_memory (const void *vaddr);
static int get_new_fd(void);
static struct file_fd *get_file_fd (int fd);

static void halt (void);
static void exit (int status);
static int wait (tid_t tid);
static bool remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static int read (int fd, void *buffer, unsigned size);
tid_t exec (const char *cmd_line);
static int write (int fd, const void *buffer, unsigned size);
static bool create(const char *file, unsigned initial_size);
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static void close (int fd);

void
syscall_init (void) 
{
  /* Initialise the used lists. */
  list_init(&statuses);
  list_init(&parents);
  /* Initialise the next available fd to 2; 0 and 1 reserved for STD[IN/OUT]. */
  fd = 2;
  /* Initialise the file system lock. */
  lock_init(&file_lock);
  /* Register the system call handler on 0x30. */
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  uint32_t *sp = f->esp;
  switch (*sp)
    {
    case SYS_HALT:                   /* Halt the operating system. */
        halt();
        break;
    case SYS_EXIT:                   /* Terminate this process. */
        f->eax = *(sp + 1);
        exit (*(sp + 1));
        break;
    case SYS_EXEC:                   /* Start another process. */
        f->eax = exec ((char *) *(sp + 1));
        break;
    case SYS_WAIT:                   /* Wait for a child process to die. */
        f->eax = wait (*(sp + 1));
        break;
    case SYS_CREATE:                 /* Create a file. */
        f->eax = create ((char *) *(sp + 1), (unsigned) *(sp + 2));
        break;
    case SYS_REMOVE:                 /* Delete a file. */
        f->eax = remove ((char *) *(sp + 1));
        break;
    case SYS_OPEN:                   /* Open a file. */
        f->eax = open ((char *) *(sp + 1));
        break;
    case SYS_FILESIZE:               /* Obtain a file's size. */
        f->eax = filesize ((int) *(sp + 1));
        break;
    case SYS_READ:                   /* Read from a file. */
        f->eax = read (*(sp + 1), (void *) *(sp + 2), *(sp + 3));
        break;
    case SYS_WRITE:                  /* Write to a file. */
        f->eax = write (*(sp + 1), (void *) *(sp + 2), *(sp + 3));
        break;
    case SYS_SEEK:                   /* Change position in a file. */
        seek (*(sp + 1), (unsigned) *(sp + 2));
        break;
    case SYS_TELL:                   /* Report current position in a file. */
        f->eax = tell (*(sp + 1));
        break;
    case SYS_CLOSE:                  /* Close a file. */
        close (*(sp + 1));
        break;
    }
}

static void *
syscall_user_memory (const void *vaddr)
{
  if (is_user_vaddr (vaddr))
    return pagedir_get_page (thread_current ()->pagedir, vaddr);
  else
    return NULL;
}

/* Returns and then increments the next available file descriptor. */
static int
get_new_fd(void)
{
  return fd++;
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
    NOT_REACHED();
}

static void
exit (int status)
{
  /* Add the about-to-die process to the history list of exit statuses. */
  add_process(thread_current()->tid, status);
  sema_up(get_parent_semaphore(thread_current()->parent_tid));
  printf ("%s: exit(%d)\n", thread_current()->name, status);
  thread_exit ();
}

tid_t
exec (const char *cmd_line)
{
  // TODO
  if (syscall_user_memory (cmd_line) == NULL)
    exit (-1);
  tid_t new_proc_tid = process_execute(cmd_line);
  return new_proc_tid;
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
      file_fd->fd = get_new_fd ();
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

void
add_process (tid_t tid, int status)
{
  struct exitstatus *new_process = malloc (sizeof(struct exitstatus));
  if (new_process == NULL)
    thread_exit ();
  new_process->tid = tid;
  new_process->waited_on = false;
  new_process->status = status;
  list_push_front (&statuses, &new_process->statuselem);
}

struct semaphore*
add_parent (tid_t tid)
{
  struct list_elem *e;
  for (e = list_begin (&parents); e != list_end (&parents); e = list_next (e))
    {
      struct process_sema *p_s = list_entry (e, struct process_sema,
                                             process_sema_elem);
      if (p_s->tid == tid)
        return &p_s->sema;
    }

  struct process_sema *new_parent = malloc (sizeof(struct process_sema));
  if (new_parent == NULL)
    thread_exit ();
  new_parent->tid = tid;
  sema_init (&new_parent->sema, 0);
  list_push_front (&parents, &new_parent->process_sema_elem);
  return &new_parent->sema;
}

void
remove_parent (tid_t tid)
{
  struct list_elem *e;
  for (e = list_begin (&parents); e != list_end (&parents); e = list_next (e))
    {
      struct process_sema *p_s = list_entry (e, struct process_sema,
                                             process_sema_elem);
      if (p_s->tid == tid)
        break;
    }
  if (e != list_end (&parents))
    {
      struct process_sema *p_s = list_entry (e, struct process_sema,
                                             process_sema_elem);
      list_remove (e);
      free (p_s);
    }
}

struct semaphore*
get_parent_semaphore (tid_t parent_tid)
{
  struct list_elem *e;
  for (e = list_begin (&parents); e != list_end (&parents); e = list_next (e))
    {
      struct process_sema *p_s = list_entry (e, struct process_sema,
                                             process_sema_elem);
      if (p_s->tid == parent_tid)
        return &p_s->sema;
    }
  NOT_REACHED()
}

int
get_exit_code (tid_t tid)
{
  struct list_elem *e;
  for (e = list_begin (&statuses); e != list_end (&statuses);
       e = list_next (e))
    {
      struct exitstatus *e_s = list_entry (e, struct exitstatus, statuselem);
      if (e_s->tid == tid)
        return e_s->status;
    }
  NOT_REACHED()
}

void
set_exit_code (tid_t tid, int status)
{
  struct list_elem *e;
  for (e = list_begin (&statuses); e != list_end (&statuses);
       e = list_next (e))
    {
      struct exitstatus *e_s = list_entry (e, struct exitstatus, statuselem);
      if (e_s->tid == tid)
        {
          e_s->status = status;
          return;
        }
    }
}

bool
is_waited_on (tid_t tid)
{
  struct list_elem *e;
  for (e = list_begin (&statuses); e != list_end (&statuses);
       e = list_next (e))
    {
      struct exitstatus *e_s = list_entry (e, struct exitstatus, statuselem);
      if (e_s->tid == tid)
        return e_s->waited_on;
    }
  return false;
  NOT_REACHED()
}

void
set_waited_on (tid_t tid)
{
  struct list_elem *e;
  for (e = list_begin (&statuses); e != list_end (&statuses);
       e = list_next (e))
    {
      struct exitstatus *e_s = list_entry (e, struct exitstatus, statuselem);
      if (e_s->tid == tid)
        {
          e_s->waited_on = true;
          return;
        }
    }
}

bool
is_dead (tid_t tid)
{
  struct list_elem *e;
  for (e = list_begin (&statuses); e != list_end (&statuses);
       e = list_next (e))
    {
      struct exitstatus *e_s = list_entry (e, struct exitstatus, statuselem);
      if (e_s->tid == tid)
        return true;
    }
  return false;
}
