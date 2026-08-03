/* Locate the shared object symbol nearest a given address.
   Copyright (C) 1996-2026 Free Software Foundation, Inc.
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

#include <dlfcn.h>
#include <stddef.h>
#include <ldsodefs.h>

/* Stock glibc finds the containing object with _dl_find_dso_for_object and then searches its symbol
   table by hand, reaching l_info[DT_SYMTAB], l_info[DT_STRTAB], l_info[DT_STRSZ] and one of the two
   hash tables to do it.

   The loader answers instead.  Address to symbol is a lookup it already implements for its own
   relocation work, and those four l_info entries were the last thing in libc that wanted a link map
   of its own -- forwarding is what makes keeping none possible rather than merely tidy.

   SYMBOLP is what dladdr1's RTLD_DL_SYMENT wants: the ElfW(Sym) the answer came from.  The loader
   found it either way in order to fill in dli_sname, so it hands it back rather than making this
   half search a symbol table it has no business reading.  */

int
_dl_addr (const void *address, Dl_info *info,
	  struct link_map **mapp, const ElfW(Sym) **symbolp)
{
  int result = GL (dl_bundle_runtime)->DescribeAddress (address, info,
						        (const void **) symbolp);

  if (mapp != NULL)
    {
      *mapp = NULL;

      /* The loader's own map for the containing object, which is what dlfo_link_map is.  Only the
	 five fields of the public struct link_map in <link.h> may be read from it -- l_addr, l_name,
	 l_ld, l_next, l_prev -- because that is the layout both halves agree on.  Anything further
	 in is at an offset only one build of glibc knows, and the loader is not that build.

	 Every caller in the tree reads l_addr and nothing else.  */
      if (result != 0)
	{
	  struct dl_find_object object;

	  if (GL (dl_bundle_runtime)->DescribeObject (address, &object) == 0)
	    *mapp = object.dlfo_link_map;
	}
    }

  return result;
}
