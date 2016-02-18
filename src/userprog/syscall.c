#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"

struct exitstatus
  {
    struct list_elem statuselem;
    pid_t pid;
    int status;
    bool waited_on;
  };

struct list statuses;

static void syscall_handler (struct intr_frame *);
static void *syscall_user_memory (const void *vaddr);

void exit (void);
int write (int fd, const void *buffer, unsigned size);

void
syscall_init (void) 
{
  list_init(statuses);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  void *sp = syscall_user_memory (f->esp);
  switch ((int) sp)
    {
    case SYS_HALT:                   /* Halt the operating system. */
        halt();
        break;
    case SYS_EXIT:                   /* Terminate this process. */
        f->eax = (int) (sp + 4);
        exit ();
        break;
    case SYS_EXEC:                   /* Start another process. */
        f->eax = exec ((char) (sp + 4));
        break;
    case SYS_WAIT:                   /* Wait for a child process to die. */
        f->eax = wait ((int) (sp + 4));
        break;
    case SYS_CREATE:                 /* Create a file. */
        f->eax = create ((char) (sp + 4), (unsigned) (sp + 8));
        break;
    case SYS_REMOVE:                 /* Delete a file. */
        f->eax = remove ((char) (sp + 4));
        break;
    case SYS_OPEN:                   /* Open a file. */
        f->eax = open ((char) (sp + 4));
        break;
    case SYS_FILESIZE:               /* Obtain a file's size. */
        f->eax = filesize ((int) (sp + 4));
        break;
    case SYS_READ:                   /* Read from a file. */
        f->eax = read ((int) (sp + 4), sp + 8, (unsigned) (sp + 12));
        break;
    case SYS_WRITE:                  /* Write to a file. */
        f->eax = write ((int) (sp + 4), sp + 8, (unsigned) (sp + 12));
        break;
    case SYS_SEEK:                   /* Change position in a file. */
        f->eax = seek ((int) (sp + 4), (unsigned) (sp + 8));
        break;
    case SYS_TELL:                   /* Report current position in a file. */
        f->eax = tell ((int) (sp + 4));
        break;
    case SYS_CLOSE:                  /* Close a file. */
        f->eax = close ((int) (sp + 4));
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

void
halt (void)
{
    shutdown_power_off ();
}

void
exit (void)
{
  thread_exit ();
}

pid_t
exec (const char *cmd_line)
{
    return -1;
}

int
wait (pid_t pid)
{
    return -1;
}

bool
create (const char *file, unsigned initial_size)
{
    return false;
}

bool
remove (const char *file)
{
    return false;
}

int
open (const char *file)
{
    return -1;
}

int
filesize (int fd)
{
    return -1;
}

int
read (int fd, void *buffer, unsigned size)
{
    return -1;
}

int
write (int fd, const void *buffer, unsigned size)
{
  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      return size;
    }
  else
    return 0;
}

void
seek (int fd, unsigned position)
{
    return;
}

unsigned
tell (int fd)
{
    return -1;
}

void
close (int fd)
{
    return;
}
