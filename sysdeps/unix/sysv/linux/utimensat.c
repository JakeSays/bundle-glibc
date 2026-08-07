/* Change access and modification times of open file.  Linux version.
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
#include <sys/stat.h>
#include <sysdep.h>
#include <time.h>
#include <kernel-features.h>

/* Helper function defined for easy reusage of the code which calls utimensat
   and utimensat_time64 syscall.  */
int
__utimensat64_helper (int fd, const char *file,
                      const struct __timespec64 tsp64[2], int flags)
{
  /* An image is read-only, and its timestamps are the bundler's: they are the same in every copy of
     the artifact, which is what makes two builds of one tree comparable. Guarded in the helper rather
     than at each syscall below, since those are one operation reached by whichever the running kernel
     has -- and futimens arrives here too, with the descriptor's own path.  */
#if IS_IN (libc)
  if (__bfs_bundles_at (fd, file))
    {
      __set_errno (EROFS);
      return -1;
    }
#endif

#ifndef __NR_utimensat_time64
# define __NR_utimensat_time64 __NR_utimensat
#endif

#ifdef __ASSUME_TIME64_SYSCALLS
  return INLINE_SYSCALL_CALL (utimensat_time64, fd, file, &tsp64[0], flags);
#else
  /* For UTIME_NOW and UTIME_OMIT the value of tv_sec field is ignored.  */
# define TS_SPECIAL(ts) \
  ((ts).tv_nsec == UTIME_NOW || (ts).tv_nsec == UTIME_OMIT)

  bool need_time64 = tsp64 != NULL
		     && ((!TS_SPECIAL (tsp64[0])
			  && !in_int32_t_range (tsp64[0].tv_sec))
			 || (!TS_SPECIAL (tsp64[1])
			     && !in_int32_t_range (tsp64[1].tv_sec)));
  if (need_time64)
    {
      int r = INLINE_SYSCALL_CALL (utimensat_time64, fd, file, &tsp64[0],
				   flags);
      if (r == 0 || errno != ENOSYS)
	return r;
      __set_errno (EOVERFLOW);
      return -1;
    }

  struct timespec tsp32[2], *ptsp32 = NULL;
  if (tsp64)
    {
      tsp32[0] = valid_timespec64_to_timespec (tsp64[0]);
      tsp32[1] = valid_timespec64_to_timespec (tsp64[1]);
      ptsp32 = tsp32;
    }

  return INLINE_SYSCALL_CALL (utimensat, fd, file, ptsp32, flags);
#endif
}
libc_hidden_def (__utimensat64_helper)

/* Change the access time of FILE to TSP[0] and
   the modification time of FILE to TSP[1].

   Starting with 2.6.22 the Linux kernel has the utimensat syscall.  */
int
__utimensat64 (int fd, const char *file, const struct __timespec64 tsp64[2],
               int flags)
{
  return __utimensat64_helper (fd, file, &tsp64[0], flags);
}

#if __TIMESIZE != 64
libc_hidden_def (__utimensat64)

int
__utimensat (int fd, const char *file, const struct timespec tsp[2],
             int flags)
{
  struct __timespec64 tsp64[2];
  if (tsp)
    {
      tsp64[0] = valid_timespec_to_timespec64 (tsp[0]);
      tsp64[1] = valid_timespec_to_timespec64 (tsp[1]);
    }

  return __utimensat64 (fd, file, tsp ? &tsp64[0] : NULL, flags);
}
#endif
weak_alias (__utimensat, utimensat)
