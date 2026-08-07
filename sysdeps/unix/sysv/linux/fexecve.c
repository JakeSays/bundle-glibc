/* Copyright (C) 1994-2026 Free Software Foundation, Inc.
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

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <bundle-exec.h>
#include <bundlefs-descriptors.h>
#include <fd_to_filename.h>
#include <sysdep.h>
#include <sys/syscall.h>
#include <kernel-features.h>


/* Execute the file FD refers to, overlaying the running program image.
   ARGV and ENVP are passed to the new program, as for `execve'.  */
int
fexecve (int fd, char *const argv[], char *const envp[])
{
  if (fd < 0 || argv == NULL || envp == NULL)
    {
      __set_errno (EINVAL);
      return -1;
    }

#if IS_IN (libc)
  /* A descriptor the image owns names a member, and the kernel cannot exec one: the number is an
     anonymous file, or a duplicate of the artifact positioned at the member's offset.  Left alone,
     execveat on it would run the artifact from its start -- which is to say whichever program a bare
     launch runs, not the one this descriptor was opened for.

     So the member is turned back into the program it is, by where its bytes sit, and the relaunch
     names that.  A carried file that is not a program has nothing to name and is refused: ENOEXEC
     rather than EACCES, since the file is readable and only exec'ing it is impossible.  */
  if (BfsOwns (fd))
    {
      const char *program = __bundle_program_at (fd);

      if (program == NULL)
	{
	  __set_errno (ENOEXEC);
	  return -1;
	}

      return __bundle_exec_program (program, argv, envp);
    }
#endif

#ifdef __NR_execveat
  /* Avoid implicit array coercion in syscall macros.  */
  INLINE_SYSCALL (execveat, 5, fd, "", &argv[0], &envp[0], AT_EMPTY_PATH);
# ifndef __ASSUME_EXECVEAT
  if (errno != ENOSYS)
    return -1;
# endif
#endif

#ifndef __ASSUME_EXECVEAT
  /* We use the /proc filesystem to get the information.  If it is not
     mounted we fail.  We do not need the return value.  */
  struct fd_to_filename filename;
  __execve (__fd_to_filename (fd, &filename), argv, envp);

  int save = errno;

  /* We come here only if the 'execve' call fails.  Determine whether
     /proc is mounted.  If not we return ENOSYS.  */
  struct __stat64_t64 st;
  if (__stat64_time64 ("/proc/self/fd", &st) != 0 && errno == ENOENT)
    save = ENOSYS;

  __set_errno (save);
#endif

  return -1;
}
