/* Early initialization of libc.so, libc.so side.
   Copyright (C) 2020-2026 Free Software Foundation, Inc.
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

#include <ctype.h>
#include <libc-early-init.h>
#include <libc-internal.h>
#include <lowlevellock.h>
#include <pthread_early_init.h>
#include <sys/single_threaded.h>
#include <getrandom-internal.h>
#include <malloc/malloc-internal.h>
#include <bundlefs-early-init.h>

#ifdef SHARED
_Bool __libc_initial;
#endif

void
__libc_early_init (_Bool initial)
{
#ifdef SHARED
  /* The environment the app carries, if it carries one, before anything can read a variable and --
     just as important -- before the loader performs the copy relocations.

     A program that names environ directly takes a copy of it, and that copy is made once. Setting
     __environ in libc's own ELF constructor is too late for it: constructors run after relocation,
     so the copy would already hold whatever was there before, which is nothing. Here is early
     enough, because the loader calls this before it copies anything and before any constructor.

     csu/init-first.c sets it again from the same source, because that constructor assigns __environ
     from the stack unconditionally and would otherwise put the stack's block back.  */
  if (GL (dl_bundle_runtime) != NULL
      && GL (dl_bundle_runtime)->GetEnvironment != NULL)
    {
      char **carried = GL (dl_bundle_runtime)->GetEnvironment ();

      if (carried != NULL)
	__environ = carried;
    }
#endif

  /* Initialize ctype data.  */
  __ctype_init ();

  /* Only the outer namespace is marked as single-threaded.  */
  __libc_single_threaded = initial;

#ifdef SHARED
  __libc_single_threaded_internal = __libc_initial = initial;
#endif

  __pthread_early_init ();

  __getrandom_early_init (initial);

  /* Initialize system malloc (needs __libc_initial to be set).  */
  call_function_static_weak (__ptmalloc_init);

  /* Bring up the bundle filesystem, which needs a real allocator and so goes after malloc.  Nothing
     before this reads a file, and the first file access that is not the loader resolving a shared
     object happens in a constructor, which is after all of it.  */
  __bfs_early_init ();
}
