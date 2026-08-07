/* Create a special or ordinary file.  Linux version.
   Copyright (C) 2020-2026 Free Software Foundation, Inc.
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
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <sysdep.h>

int
__mknodat (int fd, const char *path, mode_t mode, dev_t dev)
{
  /* The same pair mkdir checks: the name itself, or the directory it would go in. mkfifo arrives
     here, which is how a named pipe beside bundled content is refused rather than being created on
     the machine at a path the artifact claimed.  */
#if IS_IN (libc)
  if (__bfs_bundles_at (fd, path) || __bfs_bundles_parent_at (fd, path))
    {
      __set_errno (EROFS);
      return -1;
    }
#endif

  /* The user-exported dev_t is 64-bit while the kernel interface is
     32-bit.  */
  unsigned int k_dev = dev;
  if (k_dev != dev)
    return INLINE_SYSCALL_ERROR_RETURN_VALUE (EINVAL);

  return INLINE_SYSCALL_CALL (mknodat, fd, path, mode, k_dev);
}
libc_hidden_def (__mknodat)
weak_alias (__mknodat, mknodat)
