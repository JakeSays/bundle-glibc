/* Duplicate a file descriptor.  Linux version.
   Copyright (C) 2011-2026 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library.  If not, see
   <https://www.gnu.org/licenses/>.  */

#include <bundlefs-descriptors.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <not-cancel.h>
#include <sysdep.h>

/* Duplicate FD to FD2, closing the old FD2 and making FD2 be
   open the same file as FD is.  Return FD2 or -1.  */
int
__dup2 (int fd, int fd2)
{
#ifdef __NR_dup2
  int copy = INLINE_SYSCALL_CALL (dup2, fd, fd2);
#else
  /* For the degenerate case, check if the fd is valid (by trying to
     get the file status flags) and return it, or else return EBADF.  */
  int copy;

  if (fd == fd2)
    copy = __libc_fcntl (fd, F_GETFL, 0) < 0 ? -1 : fd;
  else
    copy = INLINE_SYSCALL_CALL (dup3, fd, fd2, 0);
#endif

  /* The kernel has made the number; the table has to learn that it names the same file. FD2 is
     dropped whether or not FD is ours, since the kernel closed whatever it named.  */
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
libc_hidden_def (__dup2)
weak_alias (__dup2, dup2)
