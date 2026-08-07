/* Make a new name for a file.  Linux version.
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
#include <unistd.h>
#include <fcntl.h>
#include <sysdep.h>

/* Make a link to FROM called TO.  */
int
__symlink (const char *from, const char *to)
{
  /* Only where the link would be created. What it points at is a string the link stores and never
     resolves here, so a target inside the image is a target like any other.  */
#if IS_IN (libc)
  if (__bfs_bundles_at (AT_FDCWD, to) || __bfs_bundles_parent_at (AT_FDCWD, to))
    {
      __set_errno (EROFS);
      return -1;
    }
#endif

#ifdef __NR_symlink
  return INLINE_SYSCALL_CALL (symlink, from, to);
#else
  return INLINE_SYSCALL_CALL (symlinkat, from, AT_FDCWD, to);
#endif
}
weak_alias (__symlink, symlink)
