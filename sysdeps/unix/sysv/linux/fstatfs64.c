/* Return information about the filesystem on which FD resides.
   Copyright (C) 1996-2026 Free Software Foundation, Inc.
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

#define __fstatfs __fstatfs_disable
#define fstatfs fstatfs_disable
#include <bundlefs-descriptors.h>
#include <sys/statfs.h>
#include <sysdep.h>
#include <kernel_stat.h>
#undef __fstatfs
#undef fstatfs

/* Return information about the filesystem on which FD resides.  */
int
__fstatfs64 (int fd, struct statfs64 *buf)
{
  /* See statfs64.c. The number underneath one of our descriptors is an anonymous file, so the kernel
     would describe the tmpfs behind it -- which is neither the image nor the machine.  */
#if IS_IN (libc)
  BfsFileSystemInfo bundled;

  if (BfsDescribeFileSystem (fd, &bundled) == 0)
    {
      __bfs_fill_statfs (&bundled, buf);
      return 0;
    }
#endif

#ifdef __NR_fstatfs64
  return INLINE_SYSCALL_CALL (fstatfs64, fd, sizeof (*buf), buf);
#else
  return INLINE_SYSCALL_CALL (fstatfs, fd, buf);
#endif
}
weak_alias (__fstatfs64, fstatfs64)

#if STATFS_IS_STATFS64
weak_alias (__fstatfs64, __fstatfs)
weak_alias (__fstatfs64, fstatfs)
libc_hidden_ver (__fstatfs64, __fstatfs)
#endif
