/* Linux implementation for access function.
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
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sysdep-cancel.h>

int
__access (const char *file, int type)
{
  /* An image cannot be written, so a question about writing falls through and gets the machine's
     answer -- which is no, there being nothing there.
     Executing is a different question and has to be answered from the image's own mode bits. For a
     directory X_OK is whether it can be searched, and refusing that is how realpath came to fail on
     every carried path: it walks the components asking exactly this, and a refusal here sent it to a
     kernel that has never heard of them.  */
#if IS_IN (libc)
  if ((type & W_OK) == 0)
    {
      struct stat64 described;

      if (__bfs_stat_path (file, &described) == 0)
	{
	  if ((type & X_OK) == 0
	      || (described.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0)
	    return 0;

	  __set_errno (EACCES);
	  return -1;
	}

      /* Not carried, and the reason is the artifact's to give -- see open64.c.  */
      if (__bfs_reason_missing (file) == ENOTDIR)
	{
	  __set_errno (ENOTDIR);
	  return -1;
	}

      /* Under a mount the artifact keeps to itself, and it does not hold this -- see open64.c.  */
      if (__bfs_claims (file))
	{
	  __set_errno (ENOENT);
	  return -1;
	}
    }
#endif

#ifdef __NR_access
  return INLINE_SYSCALL_CALL (access, file, type);
#else
  return INLINE_SYSCALL_CALL (faccessat, AT_FDCWD, file, type);
#endif
}
libc_hidden_def (__access)
weak_alias (__access, access)
