/* mmap - map files or devices into memory.  Linux version.
   Copyright (C) 1999-2026 Free Software Foundation, Inc.
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
#include <unistd.h>
#include <sys/mman.h>
#include <sysdep.h>
#include <mmap_internal.h>

#ifdef __NR_mmap2
/* To avoid silent truncation of offset when using mmap2, do not accept
   offset larger than 1 << (page_shift + off_t bits).  For archictures with
   32 bits off_t and page size of 4096 it would be 1^44.  */
# define MMAP_OFF_HIGH_MASK \
  ((-(MMAP2_PAGE_UNIT << 1) << (8 * sizeof (off_t) - 1)))
#else
/* Some ABIs might use __NR_mmap while having sizeof (off_t) smaller than
   sizeof (off64_t) (currently only MIPS64n32).  For this case just set
   zero the higher bits so mmap with large offset does not fail.  */
# define MMAP_OFF_HIGH_MASK  0x0
#endif

#define MMAP_OFF_MASK (MMAP_OFF_HIGH_MASK | MMAP_OFF_LOW_MASK)

/* An architecture may override this.  */
#ifndef MMAP_PREPARE
# define MMAP_PREPARE(addr, len, prot, flags, fd, offset)
#endif

void *
__mmap64 (void *addr, size_t len, int prot, int flags, int fd, off64_t offset)
{
  MMAP_CHECK_PAGE_UNIT ();

  if (offset & MMAP_OFF_MASK)
    return (void *) INLINE_SYSCALL_ERROR_RETURN_VALUE (EINVAL);

  /* A descriptor of ours holds no content, so mapping it would give a page of zeroes. The artifact is
     mapped instead, at the offset the file's bytes lie at -- which is what keeps a bundled shared
     object or asset as cheap to map as one on the machine.

     Only when a descriptor is named: MAP_ANONYMOUS ignores fd, and the table says no to everything
     else in a process that is not running out of an artifact.  */
#if IS_IN (libc)
  if ((flags & MAP_ANONYMOUS) == 0 && BfsOwns (fd))
    return BfsMap (addr, len, prot, flags, fd, offset);
#endif

  MMAP_PREPARE (addr, len, prot, flags, fd, offset);
#ifdef __NR_mmap2
  return (void *) MMAP_CALL (mmap2, addr, len, prot, flags, fd,
			     (off_t) (offset / MMAP2_PAGE_UNIT));
#else
  return (void *) MMAP_CALL (mmap, addr, len, prot, flags, fd, offset);
#endif
}
weak_alias (__mmap64, mmap64)
libc_hidden_def (__mmap64)

#ifdef __OFF_T_MATCHES_OFF64_T
static_weak_alias (__mmap64, mmap)
strong_alias (__mmap64, __mmap)
libc_hidden_def (__mmap)
#endif
