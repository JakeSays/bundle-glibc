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
__link (const char *from, const char *to)
{
  /* Both sides, unlike symlink. A hard link needs the file itself on the other end, and a carried one
     is a record in an image rather than an inode anything can point a second name at.  */
#if IS_IN (libc)
  if (__bfs_bundles_at (AT_FDCWD, from) || __bfs_bundles_at (AT_FDCWD, to)
      || __bfs_bundles_parent_at (AT_FDCWD, to))
    {
      __set_errno (EROFS);
      return -1;
    }
#endif

#ifdef __NR_link
  return INLINE_SYSCALL_CALL (link, from, to);
#else
  return INLINE_SYSCALL_CALL (linkat, AT_FDCWD, from, AT_FDCWD, to, 0);
#endif
}

weak_alias (__link, link)
