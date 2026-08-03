/* Locating objects in the process image.  libc forwarder.
   Copyright (C) 2021-2026 Free Software Foundation, Inc.
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

#include <ldsodefs.h>
#include <dlfcn.h>

/* Stock glibc forwards to GLRO (dl_find_object), which answers from a sorted lock-free array the
   loader maintains across dlopen and dlclose.  That array is built by walking the list of loaded
   objects, so it goes where the list went.

   This is libgcc's unwind path, and the only one it takes -- a library built by GCC against glibc
   2.35 or later reaches every C++ throw through here.  */
int
_dl_find_object (void *address, struct dl_find_object *result)
{
  return GL (dl_bundle_runtime)->DescribeObject (address, result);
}
