/* Create a directory.  Linux version.
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
#include <sys/stat.h>
#include <sysdep.h>

/* Create a directory named PATH with protections MODE.  */
int
__mkdir (const char *path, mode_t mode)
{
  /* Either the name is one the image already answers for, or the directory it would go in is. The
     second is the one a program actually asks for -- making a directory beside carried content.  */
#if IS_IN (libc)
  if (__bfs_carries_at (AT_FDCWD, path) || __bfs_carries_parent_at (AT_FDCWD, path))
    {
      __set_errno (EROFS);
      return -1;
    }
#endif

#ifdef __NR_mkdir
  return INLINE_SYSCALL_CALL (mkdir,  path, mode);
#else
  return INLINE_SYSCALL_CALL (mkdirat, AT_FDCWD, path, mode);
#endif
}

libc_hidden_def (__mkdir)
weak_alias (__mkdir, mkdir)
