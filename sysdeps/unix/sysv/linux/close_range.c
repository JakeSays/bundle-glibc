/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* close_range, as a C routine rather than the stub sysdeps/unix/sysv/linux/syscalls.list generates.
 *
 * The kernel closes the whole range in one call, touching no libc entry point on the way, so the
 * descriptor table hears nothing. posix_spawn does exactly this on every launch -- spawni.c closes
 * everything above the descriptors it is passing on -- and what it leaves behind is a table full of
 * records for numbers the kernel has taken back. The next memfd_create is then handed one of those
 * numbers with a dead file behind it, and every lookup stops at the first match.
 *
 * That is the same defect close_nocancel had, and it is worse here: one call can strand every
 * bundled descriptor in the process at once.
 *
 * The entry had to be deleted from syscalls.list rather than merely shadowed by this file.
 * make-syscalls.sh only consults directories of higher priority than the one holding the list, so a
 * source in that same directory is never seen. close_range is already named in io/Makefile's
 * routines, so nothing else was needed.  */

#include <bundlefs-descriptors.h>
#include <errno.h>
#include <unistd.h>
#include <sysdep.h>

int
__close_range (unsigned int first, unsigned int last, int flags)
{
  /* CLOSE_RANGE_CLOEXEC marks rather than closes, so the numbers stay live and stay ours.  */
#if IS_IN (libc)
  if ((flags & CLOSE_RANGE_CLOEXEC) == 0)
    __bfs_forget_range (first, last);
#endif

  return INLINE_SYSCALL_CALL (close_range, first, last, flags);
}
libc_hidden_def (__close_range)
weak_alias (__close_range, close_range)
