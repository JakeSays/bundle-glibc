/* Linux open syscall implementation, LFS.
   Copyright (C) 1991-2026 Free Software Foundation, Inc.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <bundlefs-descriptors.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sysdep-cancel.h>
#include <shlib-compat.h>

/* Open FILE with access OFLAG.  If O_CREAT or O_TMPFILE is in OFLAG,
   a third argument is the file protection.  */
int
__libc_open64 (const char *file, int oflag, ...)
{
  int mode = 0;

  if (__OPEN_NEEDS_MODE (oflag))
    {
      va_list arg;
      va_start (arg, oflag);
      mode = va_arg (arg, int);
      va_end (arg);
    }

  /* The artifact first, and the machine when it carries nothing there. A bundled path has to win:
     the point of mounting one is that the program opens what it was built against rather than
     whatever the machine happens to have at the same name.  */
#if IS_IN (libc)
  int carried = BfsOpenPath (file, oflag);
  if (carried >= 0)
    return carried;

  /* O_NOFOLLOW on a name the image has a link at. The link is there and it is what was asked about,
     so this is ELOOP and not a miss -- which is what the kernel says for the same request, and what a
     caller passing this flag is written to expect.

     Ahead of the block below because that one follows: it would describe whatever the link points at
     and report EIO for a file that is not the one in question.  */
  if ((oflag & O_NOFOLLOW) != 0)
    {
      struct stat64 link;

      if (__bfs_lstat_path (file, &link) == 0 && S_ISLNK (link.st_mode))
	{
	  __set_errno (ELOOP);
	  return -1;
	}
    }

  /* Carried, but not openable this way. Answered here rather than fallen through: the kernel has
     never heard of these paths and says ENOENT, which is a different statement -- the file is there
     and the request is what is wrong. Reporting otherwise sends a caller looking for a missing file.  */
  struct stat64 described;

  if (__bfs_stat_path (file, &described) == 0)
    {
      __set_errno ((oflag & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0
		   ? EROFS
		   : ((oflag & O_DIRECTORY) != 0 && !S_ISDIR (described.st_mode)
		      ? ENOTDIR
		      : EIO));
      return -1;
    }

  /* Not carried, and the reason is the artifact's to give: a prefix component it carries that is not
     a directory is ENOTDIR wherever it is asked.  */
  if (__bfs_reason_missing (file) == ENOTDIR)
    {
      __set_errno (ENOTDIR);
      return -1;
    }

  /* Not carried, and under a mount the artifact keeps to itself. The artifact declared this path, so
     there is nothing here -- and asking the kernel would resolve a name in the artifact's own view
     against a file it knows nothing about.  */
  if (__bfs_claims (file))
    {
      __set_errno (ENOENT);
      return -1;
    }
#endif

  return SYSCALL_CANCEL (openat, AT_FDCWD, file, oflag | O_LARGEFILE,
			 mode);
}

strong_alias (__libc_open64, __open64)
libc_hidden_weak (__open64)
weak_alias (__libc_open64, open64)

#ifdef __OFF_T_MATCHES_OFF64_T
strong_alias (__libc_open64, __libc_open)
strong_alias (__libc_open64, __open)
libc_hidden_weak (__open)
weak_alias (__libc_open64, open)
#endif

#if OTHER_SHLIB_COMPAT (libpthread, GLIBC_2_1, GLIBC_2_2)
compat_symbol (libc, __libc_open64, open64, GLIBC_2_2);
#endif
