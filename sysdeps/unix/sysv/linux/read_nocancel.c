/* Linux read syscall implementation -- non-cancellable.
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
#include <unistd.h>
#include <sysdep-cancel.h>
#include <not-cancel.h>

ssize_t
__read_nocancel (int fd, void *buf, size_t nbytes)
{
  /* The same consult read makes. This is the branch stdio takes for a stream marked
     _IO_FLAGS2_NOTCANCEL, which is what glibc's own internal streams use -- so without it a bundled
     path opened by one of those reads as an empty file.  */
#if IS_IN (libc)
  if (BfsOwns (fd))
    return BfsRead (fd, buf, nbytes);
#endif

  return INLINE_SYSCALL_CALL (read, fd, buf, nbytes);
}
hidden_def (__read_nocancel)
