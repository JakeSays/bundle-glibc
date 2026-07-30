/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* dup, as a C routine rather than the stub sysdeps/unix/syscalls.list generates.
 *
 * A number of ours is a token: the file behind it lives in the descriptor table, not in the anonymous
 * file the kernel handed out. Duplicating the number without telling the table leaves the copy naming
 * an empty file, which reads as nothing rather than failing -- the quietest way to be wrong.
 *
 * dup2 already has a C file here for its own reasons, which is what makes this work: a source in a
 * sysdeps directory overrides the syscalls.list entry for the same routine.  */

#include <bundlefs-descriptors.h>
#include <errno.h>
#include <unistd.h>
#include <not-cancel.h>
#include <sysdep.h>

int
__dup (int fd)
{
  int copy = INLINE_SYSCALL_CALL (dup, fd);

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
libc_hidden_def (__dup)
weak_alias (__dup, dup)
