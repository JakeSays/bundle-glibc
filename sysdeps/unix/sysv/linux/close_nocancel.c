/* Linux close syscall implementation -- non-cancellable.
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

int
__close_nocancel (int fd)
{
  /* The same pairing __close makes, and needed here as well: stdio and closedir come through this
     one rather than through close, so without it a descriptor is let go by the kernel while its
     file stays in the table -- and the next memfd_create is handed that number back with the old
     file behind it.  */
#if IS_IN (libc)
  __bfs_forget (fd);
#endif

  return INLINE_SYSCALL_CALL (close, fd);
}
libc_hidden_def (__close_nocancel)
