/* Copyright (C) 2011-2026 Free Software Foundation, Inc.
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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sysdep.h>


int
openat64 (int dfd, const char *file, int oflag, ...)
{
  assert (!__OPEN_NEEDS_MODE (oflag));

  return INLINE_SYSCALL (openat, 3, dfd, file, oflag | O_LARGEFILE);
}

/* The loader calls this by its internal name, which this file did not define -- so the reference
   reached libc's openat64.os instead, which was harmless until that object began consulting the
   bundle filesystem. Taking it now would pull the descriptor table into ld.so, and malloc with it.

   The loader has no use for the routing regardless: it opens the artifact itself and maps from the
   extents recorded in the view note.  */
strong_alias (openat64, __openat64)
