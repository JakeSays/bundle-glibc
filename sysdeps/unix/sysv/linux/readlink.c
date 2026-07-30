/* Read value of a symbolic link.  Linux version.
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

/* Read the contents of the symbolic link PATH into no more than
   LEN bytes of BUF.  The contents are not null-terminated.
   Returns the number of characters read, or -1 for errors.  */
ssize_t
__readlink (const char *path, char *buf, size_t len)
{
  /* A link the artifact carries. The target is recorded in the image and is not followed here, which
     is what readlink is for.

     A carried path that is not a link is EINVAL, and answering it here rather than falling through
     is the whole point: the kernel has never heard of these paths and says ENOENT, which means
     something else entirely. realpath asks this of every component of every path it is given and
     reads ENOENT as "does not exist" -- so a carried directory looked like a missing one, realpath
     failed, and Qt silently dropped every plugin path that would not canonicalize.

     Falling through is only right when the artifact carries nothing there. When it does carry the
     path, this has to answer, even when the answer is a failure.  */
#if IS_IN (libc)
  ssize_t carried = __bfs_readlink_path (path, buf, len);

  if (carried >= 0)
    return carried;

  if (__bfs_carries (path))
    {
      __set_errno (EINVAL);
      return -1;
    }
#endif

#ifdef __NR_readlink
  return INLINE_SYSCALL_CALL (readlink, path, buf, len);
#else
  return INLINE_SYSCALL_CALL (readlinkat, AT_FDCWD, path, buf, len);
#endif
}
weak_alias (__readlink, readlink)
