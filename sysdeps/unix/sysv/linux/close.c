/* Linux close syscall implementation.
   Copyright (C) 2017-2026 Free Software Foundation, Inc.
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
   License along with the GNU C Library; if not, see
   <https://www.gnu.org/licenses/>.  */

#include <bundlefs-descriptors.h>
#include <unistd.h>
#include <sysdep-cancel.h>
#include <not-cancel.h>

/* Close the file descriptor FD.  */
int
__close (int fd)
{
  /* The file behind it is let go first, and then the number, which the kernel gave out and takes
     back. Both halves or neither: forgetting without closing leaks a descriptor, and closing without
     forgetting leaves the next open able to be handed the same number with an old file behind it.  */
#if IS_IN (libc)
  __bfs_forget (fd);
#endif

  return SYSCALL_CANCEL (close, fd);
}
libc_hidden_def (__close)
strong_alias (__close, __libc_close)
weak_alias (__close, close)
