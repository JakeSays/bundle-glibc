/* Linux close syscall implementation -- non-cancellable, no errno update.
   Copyright (C) 2025-2026 Free Software Foundation, Inc.
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

void
__close_nocancel_nostatus (int fd)
{
  /* The third way a descriptor is let go, after close and close_nocancel. The closefrom fallback
     loops over this one, and it leaks the same way if it does not tell the table.  */
#if IS_IN (libc)
  __bfs_forget (fd);
#endif

  INTERNAL_SYSCALL_CALL (close, fd);
}
libc_hidden_def (__close_nocancel_nostatus)
