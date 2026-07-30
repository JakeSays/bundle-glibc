/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* dup3, as a C routine rather than the stub sysdeps/unix/syscalls.list generates. See dup.c for why
 * the table has to hear about a duplicated number, and why a source here overrides that stub.  */

#include <bundlefs-descriptors.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <not-cancel.h>
#include <sysdep.h>

int
__dup3 (int fd, int fd2, int flags)
{
  int copy = INLINE_SYSCALL_CALL (dup3, fd, fd2, flags);

#if IS_IN (libc)
  if (copy >= 0 && !__bfs_adopt (fd, copy))
    {
      __close_nocancel (copy);
      __set_errno (ENOMEM);
      return -1;
    }
#endif

  return copy;
}
libc_hidden_def (__dup3)
weak_alias (__dup3, dup3)
