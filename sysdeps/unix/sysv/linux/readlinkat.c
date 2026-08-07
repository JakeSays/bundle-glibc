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
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sysdep.h>

ssize_t
readlinkat (int fd, const char *path, char *buffer, size_t length)
{
#if IS_IN (libc)
  char resolved[PATH_MAX];

  if (BfsResolveAt (fd, path, resolved, sizeof (resolved)))
    {
      ssize_t bundled = BfsReadLinkPath (resolved, buffer, length);

      if (bundled >= 0)
	return bundled;

      /* Bundled, and the reader said why not -- EINVAL for something that is not a link, rather than
	 the kernel's ENOENT, which means something else entirely. See readlink.c.  */
      if (BfsBundles (resolved))
	{
	  __set_errno ((int) -bundled);
	  return -1;
	}
    }
#endif

  return INLINE_SYSCALL_CALL (readlinkat, fd, path, buffer, length);
}
libc_hidden_def (readlinkat)
