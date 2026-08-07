/* Linux implementation for rename function.
   Copyright (C) 2016-2026 Free Software Foundation, Inc.
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
#include <stdio.h>
#include <fcntl.h>
#include <sysdep.h>
#include <errno.h>

/* Rename the file OLD to NEW.  */
int
rename (const char *old, const char *new)
{
  /* Either side is enough to refuse. A carried source cannot be moved out of an image that has no way
     to forget it, and a carried destination -- or a new name in a carried directory -- is a place the
     image already answers for.  */
#if IS_IN (libc)
  if (__bfs_bundles_at (AT_FDCWD, old) || __bfs_bundles_at (AT_FDCWD, new)
      || __bfs_bundles_parent_at (AT_FDCWD, new))
    {
      __set_errno (EROFS);
      return -1;
    }
#endif

#if defined (__NR_rename)
  return INLINE_SYSCALL_CALL (rename, old, new);
#elif defined (__NR_renameat)
  return INLINE_SYSCALL_CALL (renameat, AT_FDCWD, old, AT_FDCWD, new);
#else
  return INLINE_SYSCALL_CALL (renameat2, AT_FDCWD, old, AT_FDCWD, new, 0);
#endif
}
