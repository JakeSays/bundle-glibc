/* Change the permissions of an open file.  Linux version.
   Copyright (C) 2026 Free Software Foundation, Inc.
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

/* A real source rather than the syscalls.list entry this replaces.

   That entry produced a wrapper with nowhere to put a guard, and a descriptor on carried content
   went straight to the kernel -- which changed the mode of the empty anonymous file underneath and
   reported success. Of everything an image had no answer for, this was the one that claimed to have
   done it.  */

#include <bundlefs-descriptors.h>
#include <errno.h>
#include <sys/stat.h>
#include <sysdep.h>

int
__fchmod (int fd, mode_t mode)
{
  /* EROFS rather than the EBADF that write and ftruncate answer with: this needs no descriptor open
     for writing, so there is nothing wrong with the descriptor. What is wrong is that the mode it
     names is the bundler's record, and the image holding it cannot be written.  */
#if IS_IN (libc)
  if (BfsOwns (fd))
    {
      __set_errno (EROFS);
      return -1;
    }
#endif

  return INLINE_SYSCALL_CALL (fchmod, fd, mode);
}
weak_alias (__fchmod, fchmod)
