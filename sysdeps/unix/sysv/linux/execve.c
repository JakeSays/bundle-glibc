/* Execute a program.  Linux version.
   Copyright (C) 2026 Free Software Foundation, Inc.
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

#include <bundle-exec.h>
#include <bundle/BundleIndex.h>
#include <bundle/BundleView.h>
#include <bundlefs-descriptors.h>
#include <errno.h>
#include <ldsodefs.h>
#include <limits.h>
#include <string.h>
#include <sysdep.h>
#include <unistd.h>

/* Relaunches the artifact with PATH named as the program to run.

   A carried executable sits inside the artifact, so there is no path the kernel can open and an
   ordinary execve of one fails with ENOENT -- which reads as "your program is missing" rather than
   as "programs live somewhere the kernel cannot see".  What reaches it is a launch of the artifact
   with the program named in the environment: the loader reads that name, matches it against the
   programs the manifest declared, and runs that one.

   /proc/self/exe rather than the path this process was started with.  It is the kernel's own record
   of what it executed, so it survives the file being renamed and the process having changed
   directory, neither of which argv[0] does -- and stage zero already falls back to it.

   Nothing here allocates.  This may be reached after vfork, where the child shares the parent's
   memory until it execs and calling malloc is undefined, so the replacement environment is built on
   the stack.  */
const char *
__bundle_program_at (int descriptor)
{
#if IS_IN (libc)
  const RuntimeInterface *runtime = GL (dl_bundle_runtime);

  if (runtime == NULL || runtime->GetBundleView == NULL)
    return NULL;

  BfsExtent extent;

  if (!BfsExtentOf (descriptor, &extent))
    return NULL;

  const struct BundleView *view = runtime->GetBundleView ();

  if (view == NULL)
    return NULL;

  const struct BundledProgram *programs
    = (const struct BundledProgram *) ((const char *) view + view->program_offset);

  const char *strings = (const char *) view + view->string_offset;

  for (uint32_t index = 0; index < view->program_count; index++)
    if (programs[index].offset == extent.Offset)
      return strings + programs[index].image_path;
#endif

  return NULL;
}

enum bundle_exec_route
__bundle_exec_route (const char *path)
{
#if IS_IN (libc)
  const RuntimeInterface *runtime = GL (dl_bundle_runtime);

  if (path != NULL && runtime != NULL)
    {
      if (runtime->IsProgram != NULL && runtime->IsProgram (path) != 0)
	return bundle_exec_relaunch;

      if (runtime->ClaimsPath != NULL && runtime->ClaimsPath (path) != 0)
	return bundle_exec_absent;
    }
#endif

  return bundle_exec_kernel;
}

int
__bundle_exec_program (const char *path, char *const argv[], char *const envp[])
{
  size_t length = strlen (path);

  if (length > PATH_MAX)
    {
      __set_errno (ENAMETOOLONG);
      return -1;
    }

  char selector[sizeof (BUNDLE_PROGRAM_VARIABLE) + PATH_MAX + 1];

  memcpy (selector, BUNDLE_PROGRAM_VARIABLE "=", sizeof (BUNDLE_PROGRAM_VARIABLE));
  memcpy (selector + sizeof (BUNDLE_PROGRAM_VARIABLE), path, length + 1);

  size_t count = 0;

  if (envp != NULL)
    while (envp[count] != NULL)
      count++;

  /* One more for the selector, one for the terminator.  */
  char *replacement[count + 2];

  size_t kept = 0;

  for (size_t i = 0; i < count; i++)
    {
      /* Any selector the caller was carrying is dropped rather than left to be found first.  It
	 would name whatever program started this process, and the loader reads the first match.  */
      if (strncmp (envp[i], BUNDLE_PROGRAM_VARIABLE "=",
		   sizeof (BUNDLE_PROGRAM_VARIABLE)) == 0)
	continue;

      replacement[kept++] = envp[i];
    }

  replacement[kept++] = selector;
  replacement[kept] = NULL;

  return INLINE_SYSCALL_CALL (execve, "/proc/self/exe", argv, replacement);
}

int
__execve (const char *path, char *const argv[], char *const envp[])
{
  switch (__bundle_exec_route (path))
    {
    case bundle_exec_relaunch:
      return __bundle_exec_program (path, argv, envp);

    /* A path the artifact claimed and carries no program at names nothing.  Left alone the kernel
       would run whatever the machine has there, which is a different program wearing the artifact's
       name -- the same reason a library at such a path is refused, and worse, because loading an
       executable is running it.  */
    case bundle_exec_absent:
      __set_errno (ENOENT);
      return -1;

    case bundle_exec_kernel:
    default:
      break;
    }

  return INLINE_SYSCALL_CALL (execve, path, argv, envp);
}
weak_alias (__execve, execve)
