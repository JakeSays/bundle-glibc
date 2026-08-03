/* Transfer data between file descriptors.  Linux version.
   Copyright (C) 2022-2026 Free Software Foundation, Inc.
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
#include <sys/sendfile.h>
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <sysdep.h>

/* Send COUNT bytes from file associated with IN_FD starting at OFFSET to
   descriptor OUT_FD.  */
ssize_t
sendfile64 (int out_fd, int in_fd, off64_t *offset, size_t count)
{
#ifndef __NR_sendfile64
# define __NR_sendfile64 __NR_sendfile
#endif

  /* The kernel copies between two descriptors it knows, and ours holds an empty anonymous file --
     it would send nothing and report success. The bytes come out of the image and go to the output
     descriptor here instead. The one unavoidable copy on the read path: the destination is somebody
     else's file, so the content has to cross.  */
#if IS_IN (libc)
  if (BfsOwns (in_fd))
    {
      char buffer[65536];
      size_t done = 0;

      while (done < count)
	{
	  size_t want = count - done;
	  if (want > sizeof (buffer))
	    want = sizeof (buffer);

	  off64_t at = offset == NULL ? 0 : *offset + (off64_t) done;

	  ssize_t got = BfsSendfileRead (in_fd, buffer, want,
					     offset == NULL ? NULL : &at);
	  if (got < 0)
	    return done > 0 ? (ssize_t) done : -1;

	  /* The end of the file.  */
	  if (got == 0)
	    break;

	  ssize_t put = __write (out_fd, buffer, (size_t) got);
	  if (put < 0)
	    return done > 0 ? (ssize_t) done : -1;

	  done += put;

	  /* A short write ends it, as it does for the syscall.  */
	  if (put < got)
	    break;
	}

      /* Advanced past what was sent, which is what sendfile does with an explicit offset -- and it
	 leaves the descriptor's own position alone, which reading positionally already did.  */
      if (offset != NULL)
	*offset += done;

      return (ssize_t) done;
    }
#endif

  return INLINE_SYSCALL_CALL (sendfile64, out_fd, in_fd, offset, count);
}

#ifdef __OFF_T_MATCHES_OFF64_T
strong_alias (sendfile64, sendfile)
#endif
