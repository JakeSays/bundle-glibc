/* Linux openat syscall implementation, LFS.
   Copyright (C) 2007-2026 Free Software Foundation, Inc.
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
#include <stdarg.h>
#include <sys/stat.h>

#include <sysdep-cancel.h>

/* Open FILE with access OFLAG.  Interpret relative paths relative to
   the directory associated with FD.  If OFLAG includes O_CREAT or
   O_TMPFILE, a fourth argument is the file protection.  */
int
__libc_openat64 (int fd, const char *file, int oflag, ...)
{
  mode_t mode = 0;
  if (__OPEN_NEEDS_MODE (oflag))
    {
      va_list arg;
      va_start (arg, oflag);
      mode = va_arg (arg, mode_t);
      va_end (arg);
    }

  /* What the artifact carries, when the path names something it does. open64 does the same against
     AT_FDCWD; this is the rest of it, since a path relative to a bundled directory has no meaning to
     the kernel -- the number it holds is an anonymous file.  */
#if IS_IN (libc)
  char resolved[PATH_MAX];

  if (BfsResolveAt (fd, file, resolved, sizeof (resolved)))
    {
      int carried = BfsOpenPath (resolved, oflag);

      if (carried >= 0)
	return carried;

      /* O_NOFOLLOW on a name the image has a link at -- see open64.c. Ahead of the block below,
	 which follows and would describe the target.  */
      if ((oflag & O_NOFOLLOW) != 0)
	{
	  struct stat64 link;

	  if (__bfs_lstat_path (resolved, &link) == 0 && S_ISLNK (link.st_mode))
	    {
	      __set_errno (ELOOP);
	      return -1;
	    }
	}

      /* Carried, but not openable this way -- see open64.c for why this is answered here rather
	 than fallen through to a kernel that has never heard of the path.  */
      struct stat64 described;

      if (__bfs_stat_path (resolved, &described) == 0)
	{
	  __set_errno ((oflag & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0
		       ? EROFS
		       : ((oflag & O_DIRECTORY) != 0 && !S_ISDIR (described.st_mode)
			  ? ENOTDIR
			  : EIO));
	  return -1;
	}

      /* Not carried, and the reason is the artifact's to give -- see open64.c.  */
      if (__bfs_reason_missing (resolved) == ENOTDIR)
	{
	  __set_errno (ENOTDIR);
	  return -1;
	}

      /* Not carried, and under a mount the artifact keeps to itself -- see open64.c.  */
      if (__bfs_claims (resolved))
	{
	  __set_errno (ENOENT);
	  return -1;
	}
    }
#endif

  return SYSCALL_CANCEL (openat, fd, file, oflag | O_LARGEFILE, mode);
}

strong_alias (__libc_openat64, __openat64)
libc_hidden_weak (__openat64)
weak_alias (__libc_openat64, openat64)

#ifdef __OFF_T_MATCHES_OFF64_T
strong_alias (__libc_openat64, __openat)
libc_hidden_weak (__openat)
weak_alias (__libc_openat64, openat)
#endif
