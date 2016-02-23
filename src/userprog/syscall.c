#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "userprog/process.h"

struct exitstatus
  {
    struct list_elem statuselem;
    tid_t tid;
    int status;
    bool waited_on;
  };

struct list statuses;

static void syscall_handler (struct intr_frame *);
static void *syscall_user_memory (const void *vaddr);

static void exit (void);
static int wait (tid_t tid);
static int write (int fd, const void *buffer, unsigned size);

void
syscall_init (void) 
{
  list_init(&statuses);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  printf("syscall_handler\n");
  uint32_t *sp = f->esp;
  hex_dump(f->esp, f->esp, 64,true);
  switch (*sp)
    {
//    case SYS_HALT:                   /* Halt the operating system. */
//        halt();
//        break;
    case SYS_EXIT:                   /* Terminate this process. */
        f->eax = (int) (sp + 4);
        exit ();
        break;
//    case SYS_EXEC:                   /* Start another process. */
//        f->eax = exec ((char) (sp + 4));
//        break;
    case SYS_WAIT:                   /* Wait for a child process to die. */
        f->eax = wait ((int) (sp + 4));
        break;
//    case SYS_CREATE:                 /* Create a file. */
//        f->eax = create ((char) (sp + 4), (unsigned) (sp + 8));
//        break;
//    case SYS_REMOVE:                 /* Delete a file. */
//        f->eax = remove ((char) (sp + 4));
//        break;
//    case SYS_OPEN:                   /* Open a file. */
//        f->eax = open ((char) (sp + 4));
//        break;
//    case SYS_FILESIZE:               /* Obtain a file's size. */
//        f->eax = filesize ((int) (sp + 4));
//        break;
//    case SYS_READ:                   /* Read from a file. */
//        f->eax = read ((int) (sp + 4), sp + 8, (unsigned) (sp + 12));
//        break;
    case SYS_WRITE:                  /* Write to a file. */
        f->eax = write (*(sp + 1), (void *) *(sp + 2), *(sp + 3));
        break;
//    case SYS_SEEK:                   /* Change position in a file. */
//        f->eax = seek ((int) (sp + 4), (unsigned) (sp + 8));
//        break;
//    case SYS_TELL:                   /* Report current position in a file. */
//        f->eax = tell ((int) (sp + 4));
//        break;
//    case SYS_CLOSE:                  /* Close a file. */
//        f->eax = close ((int) (sp + 4));
//        break;
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

//void
//halt (void)
//{
//    shutdown_power_off ();
//}

static void
exit (void)
{
  process_exit ();
  thread_exit ();
}

//tid_t
//exec (const char *cmd_line)
//{
//    return -1;
//}

static int
wait (tid_t tid)
{
  if (!is_child (tid) && is_waited_on (tid))
    return -1;

  if (is_dead (tid))
    return get_exit_code (tid);

  return process_wait (tid);
}

//bool
//create (const char *file, unsigned initial_size)
//{
//    return false;
//}
//
//bool
//remove (const char *file)
//{
//    return false;
//}
//
//int
//open (const char *file)
//{
//    return -1;
//}
//
//int
//filesize (int fd)
//{
//    return -1;
//}
//
//int
//read (int fd, void *buffer, unsigned size)
//{
//    return -1;
//}

static int
write (int fd, const void *buffer, unsigned size)
{
  printf("fd: %d\nsize: %d\n", fd, size);
  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      return size;
    }
  else
    return 0;
}
//
//void
//seek (int fd, unsigned position)
//{
//    return;
//}
//
//unsigned
//tell (int fd)
//{
//    return -1;
//}
//
//void
//close (int fd)
//{
//    return;
//}

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
  for (e = list_begin (&statuses); e != list_end (&statuses); e = list_next (e))
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
  NOT_REACHED()
}

void
set_waited_on (tid_t tid, bool waited_on)
{
  struct list_elem *e;
  for (e = list_begin (&statuses); e != list_end (&statuses);
       e = list_next (e))
    {
      struct exitstatus *e_s = list_entry (e, struct exitstatus, statuselem);
      if (e_s->tid == tid)
        {
          e_s->waited_on = waited_on;
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
