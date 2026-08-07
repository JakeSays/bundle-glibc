/* Change the current working directory to a descriptor.  Linux version.
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
   License along with the GNU C Library.  If not, see
   <https://www.gnu.org/licenses/>.  */

#include <bundlefs-descriptors.h>
#include <errno.h>
#include <unistd.h>
#include <sysdep.h>

int
__fchdir (int fd)
{
  /* See chdir.c. ENOTDIR rather than EACCES because what arrives here is a descriptor: the number
     underneath one of ours names an anonymous file, which is not a directory, and that is what the
     kernel would say about it if it could see what the caller means. musl refuses it the same way,
     with the same error.  */
#if IS_IN (libc)
  if (BfsOwns (fd))
    {
      __set_errno (ENOTDIR);
      return -1;
    }
#endif

  return INLINE_SYSCALL_CALL (fchdir, fd);
}

weak_alias (__fchdir, fchdir)
