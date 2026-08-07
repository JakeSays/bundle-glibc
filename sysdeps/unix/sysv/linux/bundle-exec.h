/* Deciding what an exec of a given path means inside an artifact.  Linux version.
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

#ifndef _BUNDLE_EXEC_H
#define _BUNDLE_EXEC_H 1

/* Shared by execve and execveat, which reach the same decision from different arguments.  Written
   once because the three cases have to stay in agreement: two wrappers that disagreed about which
   paths belong to the artifact would differ in whether a program can be started, which is the sort
   of divergence nothing would notice until it mattered.  */

enum bundle_exec_route
{
  /* Not the artifact's business.  A program on the machine, the shell, anything the payload was
     never bundled with.  */
  bundle_exec_kernel,

  /* One of the programs the manifest declared.  Reached by relaunching the artifact with it named,
     since the kernel has no path to a carried executable.  */
  bundle_exec_relaunch,

  /* Inside the artifact's own view, and not a program.  The app claimed this path by mounting over
     it, so the machine's file there is a different program wearing the app's name.  */
  bundle_exec_absent
};

extern enum bundle_exec_route __bundle_exec_route (const char *path) attribute_hidden;

/* Relaunches the artifact with PATH named as the program to run.  Returns only on failure, like
   execve.  */
extern int __bundle_exec_program (const char *path, char *const argv[],
				  char *const envp[]) attribute_hidden;

/* Which declared program a descriptor names, or NULL when it names none -- either because the
   descriptor is the machine's, or because it is a carried file that is not a program.

   Matched by where the bytes sit in the artifact.  A stat cannot answer this: size and mode are
   shared by any two members of one length, while no two members begin at the same place.  */
extern const char *__bundle_program_at (int descriptor) attribute_hidden;

#endif
