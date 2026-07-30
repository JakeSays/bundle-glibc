/* Linux statx implementation.
   Copyright (C) 2018-2026 Free Software Foundation, Inc.
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
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sysdep.h>
#include "statx_generic.c"

int
statx (int fd, const char *path, int flags,
       unsigned int mask, struct statx *buf)
{
  /* What the artifact carries, the same shapes fstatat answers: the descriptor itself when the name
     is empty and AT_EMPTY_PATH is set, otherwise a path resolved against it.  */
#if IS_IN (libc)
  if (path != NULL && path[0] == '\0' && (flags & AT_EMPTY_PATH) != 0)
    {
      if (__bfs_owns (fd) && __bfs_statx_fd (fd, buf) == 0)
	return 0;
    }
  else
    {
      char resolved[PATH_MAX];

      if (__bfs_resolve_at (fd, path, resolved, sizeof (resolved))
	  && __bfs_statx_path (resolved, buf) == 0)
	return 0;
    }
#endif

  int ret = INLINE_SYSCALL_CALL (statx, fd, path, flags, mask, buf);
#ifdef __ASSUME_STATX
  return ret;
#else
  if (ret == 0 || errno != ENOSYS)
    /* Preserve non-error/non-ENOSYS return values.  */
    return ret;
  else
    return statx_generic (fd, path, flags, mask, buf);
#endif
}
