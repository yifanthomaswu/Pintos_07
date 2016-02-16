#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);
static void *syscall_user_memory (const void *vaddr);

void exit (void);
int write (int fd, const void *buffer, unsigned size);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  void *sp = syscall_user_memory (f->esp);
  switch ((int) sp)
    {
    case SYS_EXIT:
      f->eax = (int) (sp + 4);
      exit ();
      break;
    case SYS_WRITE:
      f->eax = write((int) (sp + 4), sp + 8, (unsigned) (sp + 12));
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
exit (void)
{
  thread_exit ();
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
