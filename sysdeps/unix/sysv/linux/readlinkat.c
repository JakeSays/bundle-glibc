/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* readlinkat, as a C routine rather than the stub sysdeps/unix/sysv/linux/syscalls.list generates.
 *
 * A path relative to a descriptor has no meaning to the kernel when the descriptor is one of ours --
 * the number it holds is an anonymous file with no name in any tree -- so the join has to happen
 * here. See dup.c for why a source in this directory overrides the stub, and remember that
 * sysd-syscalls has to be regenerated before it does.  */

#include <bundlefs-descriptors.h>
#include <limits.h>
#include <unistd.h>
#include <sysdep.h>

ssize_t
readlinkat (int fd, const char *path, char *buffer, size_t length)
{
#if IS_IN (libc)
  char resolved[PATH_MAX];

  if (__bfs_resolve_at (fd, path, resolved, sizeof (resolved)))
    {
      ssize_t carried = __bfs_readlink_path (resolved, buffer, length);

      if (carried >= 0)
	return carried;
    }
#endif

  return INLINE_SYSCALL_CALL (readlinkat, fd, path, buffer, length);
}
libc_hidden_def (readlinkat)
