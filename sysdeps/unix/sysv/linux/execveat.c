/* Execute program relative to a directory file descriptor.
   Copyright (C) 2021-2026 Free Software Foundation, Inc.
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

#include <bundle-exec.h>
#include <bundlefs-descriptors.h>
#include <errno.h>
#include <fcntl.h>
#include <fd_to_filename.h>
#include <limits.h>
#include <sysdep.h>
#include <unistd.h>

/* Execute the file FD refers to, overlaying the running program image.
   ARGV and ENVP are passed to the new program, as for 'execve'.  */
int
execveat (int dirfd, const char *path, char *const argv[], char *const envp[],
          int flags)
{
#if IS_IN (libc)
  /* AT_EMPTY_PATH acts on the descriptor rather than on a name, so the member is turned back into
     the program it is by where its bytes sit, and the relaunch names that -- the same route fexecve
     takes, for the same reason.  */
  if ((flags & AT_EMPTY_PATH) != 0 && path != NULL && path[0] == '\0')
    {
      if (BfsOwns (dirfd))
	{
	  const char *program = __bundle_program_at (dirfd);

	  if (program == NULL)
	    {
	      __set_errno (ENOEXEC);
	      return -1;
	    }

	  return __bundle_exec_program (program, argv, envp);
	}
    }
  else
    {
      /* Resolved the way the *at family resolves it, so that a program named relative to a carried
	 directory is recognised as readily as one named absolutely.  */
      char resolved[PATH_MAX];

      if (BfsResolveAt (dirfd, path, resolved, sizeof resolved))
	switch (__bundle_exec_route (resolved))
	  {
	  case bundle_exec_relaunch:
	    return __bundle_exec_program (resolved, argv, envp);

	  case bundle_exec_absent:
	    __set_errno (ENOENT);
	    return -1;

	  case bundle_exec_kernel:
	  default:
	    break;
	  }
    }
#endif

  /* Avoid implicit array coercion in syscall macros.  */
  return INLINE_SYSCALL_CALL (execveat, dirfd, path, &argv[0], &envp[0],
			      flags);
}
