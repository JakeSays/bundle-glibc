/* Initialization code run first thing by the ELF startup code.  Common version
   Copyright (C) 1995-2026 Free Software Foundation, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sysdep.h>
#include <fpu_control.h>
#include <sys/param.h>
#include <sys/types.h>
#include <libc-internal.h>

#include <ldsodefs.h>

/* Remember the command line argument and environment contents for
   later calls of initializers for dynamic libraries.  */
int __libc_argc attribute_hidden;
char **__libc_argv attribute_hidden;


void
__libc_init_first (int argc, char **argv, char **envp)
{
#ifdef SHARED
  /* For DSOs we do not need __libc_init_first but an ELF constructor.  */
}

static void __attribute__ ((constructor))
_init_first (int argc, char **argv, char **envp)
{
#endif

  /* Make sure we don't initialize twice.  */
#ifdef SHARED
  if (__libc_initial)
    {
      /* Set the FPU control word to the proper default value if the
	 kernel would use a different value.  */
      if (__fpu_control != GLRO(dl_fpu_control))
	__setfpucw (__fpu_control);
    }
#endif

  /* Save the command-line arguments.  */
  __libc_argc = argc;
  __libc_argv = argv;
  __environ = envp;

#ifdef SHARED
  /* Unless the app carries an environment of its own, in which case the loader built one and this is
     where it displaces the stack's.

     Here because this is a constructor of libc itself, and every other object in the process depends
     on libc -- so this runs before any of their constructors, and long before main.  Nothing has had
     the chance to read a variable yet.

     The stack's block cannot be edited in place: the kernel writes a fixed array of fixed strings
     with no room to grow, so setting a variable that was not already there, or lengthening one that
     was, has nowhere to go.  The loader builds a new block instead and hands it over here.  */
  if (GL (dl_bundle_runtime) != NULL
      && GL (dl_bundle_runtime)->GetEnvironment != NULL)
    {
      char **carried = GL (dl_bundle_runtime)->GetEnvironment ();

      if (carried != NULL)
	__environ = carried;
    }
#endif

#ifndef SHARED
  /* First the initialization which normally would be done by the
     dynamic linker.  */
  _dl_non_dynamic_init ();
#endif

  __init_misc (argc, argv, envp);
}

/* This function is defined here so that if this file ever gets into
   ld.so we will get a link error.  Having this file silently included
   in ld.so causes disaster, because the _init_first definition above
   will cause ld.so to gain an ELF constructor, which is not a cool
   thing. */

extern void _dl_start (void) __attribute__ ((noreturn));

void
_dl_start (void)
{
  abort ();
}
