/* Linux read syscall implementation.
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

/* Read NBYTES into BUF from FD.  Return the number read or -1.  */
ssize_t
__libc_read (int fd, void *buf, size_t nbytes)
{
  /* A descriptor on something the artifact carries is read out of the image where it lies. Nothing is
     copied: the descriptor is a number the kernel gave out so it cannot collide, and the content
     stays where it was built.

     Weakly referenced, so this object linked into ld.so -- which happens, and for reasons that have
     nothing to do with reading -- resolves to nothing and goes straight to the syscall. The loader has
     no filesystem to ask, running before there is one.  */
#if IS_IN (libc)
  if (__bfs_owns (fd))
    return __bfs_read (fd, buf, nbytes);
#endif

  return SYSCALL_CANCEL (read, fd, buf, nbytes);
}
libc_hidden_def (__libc_read)

libc_hidden_def (__read)
strong_alias (__libc_read, __read)
libc_hidden_def (read)
static_weak_alias (__libc_read, read)
