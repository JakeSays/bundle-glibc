/* Change the current working directory.  Linux version.
   Copyright (C) 2026 Free Software Foundation, Inc.
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
#include <unistd.h>
#include <sysdep.h>

int
__chdir (const char *path)
{
  /* Refused, and not because an image is read-only -- this changes nothing in it. The reader has no
     notion of a current directory, so a process standing in a bundled one would resolve every
     relative path afterwards against a place the kernel cannot see, and every one of them would
     miss.

     EACCES rather than ENOENT, which is what the kernel would say about a path it has never heard
     of: the directory is there, and what cannot be done is standing in it. musl refuses the same
     call the same way, so a payload sees one behavior whichever libc it was built against.  */
#if IS_IN (libc)
  if (__bfs_bundles_at (AT_FDCWD, path))
    {
      __set_errno (EACCES);
      return -1;
    }
#endif

  return INLINE_SYSCALL_CALL (chdir, path);
}

weak_alias (__chdir, chdir)
