/* Get loaded objects program headers.
   Copyright (C) 2001-2026 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If
   not, see <https://www.gnu.org/licenses/>.  */

#include <errno.h>
#include <ldsodefs.h>
#include <stddef.h>
#include <libc-lock.h>

/* Stock glibc walks GL(dl_ns)[ns]._ns_loaded here, reading l_addr, l_name, l_phdr, l_phnum and
   l_tls_modid off each map, and holds dl_load_write_lock across the callback.

   The loader answers instead.  It is the half that owns the list of what is mapped, and forwarding
   is what lets libc keep no maps of its own -- this and _dl_addr were the two walks that would
   otherwise have forced them back.  The lock goes with the list, so the loader takes its own for the
   duration; there is nothing left here to protect.

   The callback signature is unchanged, and deliberately.  It is ABI between libc and whatever
   unwinder the application was built against, so the callback the caller supplied is passed straight
   through rather than wrapped, and the size argument still says how much of dl_phdr_info the caller
   may read.  */
int
__dl_iterate_phdr (int (*callback) (struct dl_phdr_info *info,
				    size_t size, void *data), void *data)
{
  return GL (dl_bundle_runtime)->IteratePrograms
    ((int32_t (*) (struct dl_phdr_info *, size_t, void *)) callback, data);
}
hidden_def (__dl_iterate_phdr)

weak_alias (__dl_iterate_phdr, dl_iterate_phdr);
