/* Change permissions of a file.  Linux version.
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
#include <sysdep.h>

/* Change the protections of FILE to MODE.  */
int
__chmod (const char *file, mode_t mode)
{
  /* An image is read-only, and the modes in it are the bundler's record.  */
#if IS_IN (libc)
  if (__bfs_carries_at (AT_FDCWD, file))
    {
      __set_errno (EROFS);
      return -1;
    }
#endif

#ifdef __NR_chmod
  return INLINE_SYSCALL_CALL (chmod, file, mode);
#else
  return INLINE_SYSCALL_CALL (fchmodat, AT_FDCWD, file, mode);
#endif
}

libc_hidden_def (__chmod)
weak_alias (__chmod, chmod)
