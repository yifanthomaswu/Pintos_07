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
#include "userprog/page.h"

/* Struct used to keep the history of dead processes and their exit codes. */
struct status
  {
    struct list_elem statuselem;
    tid_t tid;
    int status;
    bool dead;
  };

struct memmap
{
	struct list_elem memmapelem;
	void *addr;
	int flength;
	mapid_t mapid;
	int fd;
};

/* Lock used to synchronise any access to the file system. */
struct lock file_lock;
/* The current free file descriptor available to a process.
   Accessible through the get_new_fd () function. */
static int fd_count;

static int mapid_count;
/* History of dead processes. */

/* List of semaphores related to running process. */
static struct list processes;
/* Semaphores to synchronise access to above variables */
static struct lock fd_count_lock;
static struct lock statuses_lock;
static struct lock processes_lock;

static void syscall_handler (struct intr_frame *);
static inline int get_new_fd (void);
static struct file_fd *get_file_fd (int fd);
static inline int get_new_mapid(void);
static struct memmap *get_mapid (int fd);


static void halt (void);
static void exit (int status);
static tid_t exec (const char *cmd_line);
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
static bool list_less_file (const struct list_elem *a,
                            const struct list_elem *b,
                            void *aux UNUSED);
static bool list_less_status (const struct list_elem *a,
                              const struct list_elem *b,
                              void *aux UNUSED);
static bool list_less_process (const struct list_elem *a,
                               const struct list_elem *b,
                               void *aux UNUSED);
static bool list_less_mapid (const struct list_elem *a, const struct list_elem *b,
                void *aux UNUSED);

/* Initialise and register the system call handler. */
void
syscall_init (void)
{
  /* Initialise the next available fd to 2;
     0 and 1 reserved for STD[IN/OUT]. */
  fd_count = 2;
  /* Initialise the file system lock. */
  lock_init (&file_lock);
  /* Initialise the lists. */
  list_init (&statuses);
  list_init (&processes);
  /* Initialise the semaphores. */
  lock_init (&fd_count_lock);
  lock_init (&statuses_lock);
  lock_init (&processes_lock);
  /* Register the system call handler on 0x30. */
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Safety checking for system call arguments and redirecting system calls
   to corresponding functions. */
static void
syscall_handler (struct intr_frame *f)
{
  uint32_t *sp = f->esp;
  /* Memory accessing check for system call number. */
  if (syscall_user_memory (sp) == NULL)
    exit (-1);
  /* Variables hold the passed arguments. */
  uint32_t arg0 = 0, arg1 = 0, arg2 = 0;
  /* Memory accessing check for system call arguments. */
  switch (*sp)
    {
    case SYS_READ:
    case SYS_WRITE:
      /* Only read and write have a third argument (arg2).
         Case fallthrough avoids code duplications. */
      if (syscall_user_memory (sp + 3) == NULL)
        exit (-1);
      else
        arg2 = *(sp + 3);
    case SYS_CREATE:
    case SYS_SEEK:
      /* Create and seek both have two arguments. */
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
      /* Other syscalls with one argument. */
      if (syscall_user_memory (sp + 1) == NULL)
        exit (-1);
      else
        arg0 = *(sp + 1);
    }
  /* Calls functions corresponding to the system call number,
     providing casted arguments depending on the function declarations. */
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

/* Checks if a virtual address lies in the user address space and is mapped.
   Returns NULL otherwise. */
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
  int fd;
  lock_acquire(&fd_count_lock);
  fd = fd_count++;
  lock_release(&fd_count_lock);
  return fd;
}

static inline int
get_new_mapid (void)
{
  int mapid;
  lock_acquire(&fd_count_lock);
  mapid = mapid_count++;
  lock_release(&fd_count_lock);
  return mapid;
}

/* Returns the file_fd struct referenced by fd.
   If no file is open through fd by the current process, returns NULL. */
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
      else if (f->fd > fd)
        return NULL;
    }
  return NULL;
}

static struct memmap *
get_mapid (int mapid)
{
	 struct thread *t = thread_current();
	  struct list_elem *e;
	  for (e = list_begin (&t->mapids); e != list_end (&t->mapids);
	       e = list_next (e))
	    {
	      struct memmap *memorymap = list_entry (e, struct memmap, memmapelem);
	      if (memorymap->mapid == mapid)
	        return memorymap;
	      else if (memorymap->mapid > mapid)
	        return NULL;
	    }
	  return NULL;
}


/* Power down the machine. */
static void
halt (void)
{
  shutdown_power_off ();
}

/* Records exit code, informs its exiting, close its executable file and
   print a message before exit a thread. */
static void
exit (int status)
{
  pre_exit (status);
  thread_exit ();
}

/* Helper function used before exit a thread. */
void
pre_exit (int status)
{
  struct thread *t = thread_current ();
  /* Add the exit code of current process to history of dead processes. */
  set_status (t->tid, status);

  /* Get the semaphores of the process, if exists, up the wait semaphore. */
  struct process_sema *p_s = get_process_sema (t->parent_tid);
  if (p_s != NULL)
    sema_up (&p_s->sema_wait);

  /* Get the process' executable file, if exists, close it. */
  struct file * exec_file = t->exec_file;
  if (exec_file != NULL)
    {
      lock_acquire (&file_lock);
      file_close (exec_file);
      lock_release (&file_lock);
    }
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
}

/* Sets up a new child process, if fails, frees any used resources. */
static tid_t
exec (const char *cmd_line)
{
  if (syscall_user_memory (cmd_line) == NULL)
    exit (-1);
  tid_t new_tid = process_execute (cmd_line);
  if (new_tid != -1)
    {
      struct thread *t = thread_current ();
      struct process_sema *p_s = add_process_sema (t->tid);
      /* Parent process waits for the result of creating new process. */
      sema_down (&p_s->sema_exec);
      /* Frees used resources if new process cannot be created. */
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

/* Delegates to process_wait in process.c in normal case. If process to be
   waited on is not a child of the current process or is already waited on,
   returns -1. */
static int
wait (tid_t tid)
{
  if (!is_child (tid) || is_waited_on (tid))
    return -1;
  if (is_dead (tid))
    {
      int status = get_exit_code (tid);
      remove_status(tid);
      return status;
    }
  return process_wait (tid);
}

/* Creates a new file by delegating to filesys_create in filesys.c. */
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

/* Removes a file by delegating to filesys_remove in filesys.c. */
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

/* Opens a file by delegating to filesys_open in filesys.c. Sets up a new
   file_fd which is added to the list of files of the current process.
   Returns the file descriptor. */
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
      list_insert_ordered (&thread_current ()->files, &file_fd->filefdelem,
                           list_less_file, NULL);
      return file_fd->fd;
    }
}

/* Returns size of file by delegating to file_length in file.c. */
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

/* Reads either from standard input using input_getc or from a file using
   file_read from file.c. */
static int
read (int fd, void *buffer, unsigned size)
{
  if (syscall_user_memory (buffer) == NULL)
    exit (-1);
  if (fd == STDIN_FILENO)
    {
      /* Reads from standard input. */
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
      /* Reads from a file. */
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

/* Writes either to standard output using putbuf or to a file using
   write_file from file.c. */
static int
write (int fd, const void *buffer, unsigned size)
{
  if (syscall_user_memory (buffer) == NULL)
    exit (-1);
  if (fd == STDOUT_FILENO)
    {
      /* Writes to standard output. */
      putbuf (buffer, size);
      return size;
    }
  else if (fd == STDIN_FILENO)
    return 0;
  else
    {
      /* Writes to a file. */
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

/* Seeks position in file by delegating to file_seek in file.c. */
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

/* Returns current position in file by delegating to file_tell in file.c. */
static unsigned
tell (int fd)
{
  struct file_fd *file_fd = get_file_fd (fd);
  lock_acquire (&file_lock);
  unsigned p = file_tell (file_fd->file);
  lock_release (&file_lock);
  return p;
}

/* Closes file by delegating to file_close in file.c. Removes file from list
   of files of the current process. */
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

mapid_t
mmap (int fd, void *addr)
{
  if (addr == 0)
	  return -1;
  if (fd == STDOUT_FILENO)
	  return -1;
  else if (fd == STDIN_FILENO)
      return -1;
  if (pg_ofs(addr) != 0)
	  return -1;
  struct file_fd *filefd = get_file_fd (fd);
  int flength = file_length (filefd->file);
  if (flength == 0)
	  return -1;
  void *old_addr = addr;
  int old_flength = flength;
  while (flength > 0) {
	  if (flength % PGSIZE == 0) {
		  page_new_page (addr, PAGE_FILESYS, fd, NULL, NULL, -1, -1, -1);
		  flength -= PGSIZE;
		  addr += PGSIZE;
	  } else {
		  page_new_page (addr, PAGE_FILESYS, fd, NULL, NULL, -1, flength, PGSIZE-flength);
		  flength = 0;
		  addr += flength;
	  }
  }
  struct memmap *memorymap = malloc (sizeof (struct memmap));
  if (memorymap == NULL)
	  exit (-1);
  memorymap->fd = fd;
  memorymap->addr = old_addr;
  memorymap->flength = old_flength;
  memorymap->mapid = get_new_mapid ();
  list_insert_ordered (&mapids, &memorymap->memmapelem, list_less_mapid,
                        NULL);
  return memorymap->mapid;
}

void
munmap (mapid_t mapping)
{
	struct memmap *memorymap = get_mapid (mapping);
	while (memorymap->flength > 0)
	{
		page_remove_page (&memorymap->addr);
		if(pagedir_is_dirty(thread_current()->pagedir, &memorymap->addr)){
			if(memorymap->flength >= PGSIZE)
				write(memorymap->fd, &memorymap->addr, PGSIZE);
			else
			{
				write(memorymap->fd, &memorymap->addr, memorymap->flength);
			    memorymap->flength = 0;
			    break;
			}
		}
		memorymap->flength -= PGSIZE;
		memorymap->addr += PGSIZE;
	}
	list_remove (&memorymap->memmapelem);
	free(memorymap);
}





/* Returns process_sema corresponding to the given tid if it already exisits.
   Otherwise, sets it up and returns it. */
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
      lock_acquire(&processes_lock);
      list_insert_ordered (&processes, &p_s->process_semaelem,
                           list_less_process, NULL);
      lock_release (&processes_lock);
      return p_s;
    }
  else
    return p_s;
}

/* Returns process_sema corresponding to the given tid. */
struct process_sema *
get_process_sema (tid_t tid)
{
  struct list_elem *e;
  lock_acquire(&processes_lock);
  for (e = list_begin (&processes); e != list_end (&processes);
       e = list_next (e))
    {
      struct process_sema *p_s = list_entry (e, struct process_sema,
                                             process_semaelem);
      if (p_s->tid == tid)
        {
          lock_release (&processes_lock);
          return p_s;
        }
      else if (p_s->tid > tid)
        break;
    }
  lock_release (&processes_lock);
  return NULL;
}

/* Removes process_sema corresponding to the given tid and frees it. */
void
remove_process_sema (tid_t tid)
{
  struct process_sema *p_s = get_process_sema (tid);
  if (p_s != NULL)
    {
      lock_acquire (&processes_lock);
      list_remove (&p_s->process_semaelem);
      lock_release (&processes_lock);
      free (p_s);
    }
}

/* Adds the given exit code with corresponding tid to history
   of dead processes. */
void
add_status (tid_t tid)
{
  struct status *new_status = malloc (sizeof(struct status));
  if (new_status == NULL)
    exit (-1);
  new_status->tid = tid;
  new_status->dead = false;
  lock_acquire(&statuses_lock);
  list_insert_ordered (&statuses, &new_status->statuselem, list_less_status,
                       NULL);
  lock_release(&statuses_lock);
}

void
set_status (tid_t tid, int status)
{
  struct status *s = get_status (tid);
    if (s != NULL)
      {
        lock_acquire (&statuses_lock);
        s->status = status;
        s->dead = true;
        lock_release (&statuses_lock);
      }
}

/* Helper function used to get status struct from the history
   of dead processes corresponding to the given tid. */
static struct status *
get_status (tid_t tid)
{
  struct list_elem *e;
  lock_acquire(&statuses_lock);
  for (e = list_begin (&statuses); e != list_end (&statuses);
       e = list_next (e))
    {
      struct status *s = list_entry (e, struct status, statuselem);
      if (s->tid == tid)
        {
          lock_release(&statuses_lock);
          return s;
        }
      else if (s->tid > tid)
        break;
    }
  lock_release(&statuses_lock);
  return NULL;
}

void
remove_status(tid_t tid)
{
  struct status *s = get_status (tid);
    if (s != NULL)
      {
        lock_acquire (&statuses_lock);
        list_remove (&s->statuselem);
        lock_release (&statuses_lock);
        free (s);
      }
}

/* Returns the exit code corresponding to the given tid.
   Must not be called on a process tid which is not dead.*/
int
get_exit_code (tid_t tid)
{
  struct status *s = get_status (tid);
  if (s != NULL)
    return s->status;
  NOT_REACHED ();
}
/* Returns true if the process with the given tid has already been waited on,
   false otherwise.*/
bool
is_waited_on (tid_t tid)
{
  struct status *s = get_status (tid);
  return s == NULL;
}

/* Returns true if the process with the given tid is dead, false otherwise. */
bool
is_dead (tid_t tid)
{
  struct status *s = get_status(tid);
  if (s == NULL)
    return true;
  else
    return s->dead;
}

/* Comparison function used to insert file_fd into files list in
   ascending order of fd value. */
static bool
list_less_file (const struct list_elem *a, const struct list_elem *b,
                void *aux UNUSED)
{
  return list_entry (a, struct file_fd, filefdelem)->fd <
      list_entry (b, struct file_fd, filefdelem)->fd;
}

/* Comparison function used to insert status into statuses list in
   ascending order of tid value. */
static bool
list_less_status (const struct list_elem *a, const struct list_elem *b,
                  void *aux UNUSED)
{
  return list_entry (a, struct status, statuselem)->tid <
      list_entry (b, struct status, statuselem)->tid;
}

/* Comparison function used to insert process_sema into processes list in
   ascending order of tid value. */
static bool
list_less_process (const struct list_elem *a, const struct list_elem *b,
                   void *aux UNUSED)
{
  return list_entry (a, struct process_sema, process_semaelem)->tid <
      list_entry (b, struct process_sema, process_semaelem)->tid;
}

static bool
list_less_mapid (const struct list_elem *a, const struct list_elem *b,
                void *aux UNUSED)
{
  return list_entry (a, struct memmap, memmapelem)->mapid <
      list_entry (b, struct memmap, memmapelem)->mapid;
}
