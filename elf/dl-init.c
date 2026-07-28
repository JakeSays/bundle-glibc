/* Run initializers for newly loaded objects.
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

#include <assert.h>
#include <stddef.h>
#include <ldsodefs.h>
#include <elf-initfini.h>
#include <dl-minst.h>


static void
call_init (struct link_map *l, int argc, char **argv, char **env)
{
  /* Do not run constructors for proxy objects.  */
  if (l != l->l_real)
    return;

  /* If the object has not been relocated, this is a bug.  The
     function pointers are invalid in this case.  (Executables do not
     need relocation.)  */
  assert (l->l_relocated || l->l_type == lt_executable);

  if (l->l_init_called || l->l_proxy)
    /* This object is all done, or a proxy (and therefore initless).  */
    return;

  /* Avoid handling this constructor again in case we have a circular
     dependency.  */
  l->l_init_called = 1;

  /* Check for object which constructors we do not run here.  */
  if (__builtin_expect (l->l_name[0], 'a') == '\0'
      && l->l_type == lt_executable)
    return;

  /* Print a debug message if wanted.  */
  if (__glibc_unlikely (GLRO(dl_debug_mask) & DL_DEBUG_IMPCALLS))
    _dl_debug_printf ("\ncalling init: %s\n\n",
		      DSO_FILENAME (l->l_name));

  /* Now run the local constructors.  There are two forms of them:
     - the one named by DT_INIT
     - the others in the DT_INIT_ARRAY.
  */
  if (ELF_INITFINI && l->l_info[DT_INIT] != NULL)
    DL_CALL_DT_INIT(l, l->l_addr + l->l_info[DT_INIT]->d_un.d_ptr, argc, argv, env);

  /* Next see whether there is an array with initialization functions.  */
  ElfW(Dyn) *init_array = l->l_info[DT_INIT_ARRAY];
  if (init_array != NULL)
    {
      unsigned int j;
      unsigned int jm;
      ElfW(Addr) *addrs;

      jm = l->l_info[DT_INIT_ARRAYSZ]->d_un.d_val / sizeof (ElfW(Addr));

      addrs = (ElfW(Addr) *) (init_array->d_un.d_ptr + l->l_addr);
      for (j = 0; j < jm; ++j)
	((dl_init_t) addrs[j]) (argc, argv, env);
    }
}


void
_dl_init (struct link_map *main_map, int argc, char **argv, char **env)
{
  ElfW(Dyn) *preinit_array = main_map->l_info[DT_PREINIT_ARRAY];
  ElfW(Dyn) *preinit_array_size = main_map->l_info[DT_PREINIT_ARRAYSZ];
  unsigned int i;

  if (__glibc_unlikely (GL(dl_initfirst) != NULL))
    {
      call_init (GL(dl_initfirst), argc, argv, env);
      GL(dl_initfirst) = NULL;
    }

  /* Don't do anything if there is no preinit array.  */
  if (__builtin_expect (preinit_array != NULL, 0)
      && preinit_array_size != NULL
      && (i = preinit_array_size->d_un.d_val / sizeof (ElfW(Addr))) > 0)
    {
      ElfW(Addr) *addrs;
      unsigned int cnt;

      if (__glibc_unlikely (GLRO(dl_debug_mask) & DL_DEBUG_IMPCALLS))
	_dl_debug_printf ("\ncalling preinit: %s\n\n",
			  DSO_FILENAME (main_map->l_name));

      addrs = (ElfW(Addr) *) (preinit_array->d_un.d_ptr + main_map->l_addr);
      for (cnt = 0; cnt < i; ++cnt)
	((dl_init_t) addrs[cnt]) (argc, argv, env);
    }

  /* Stupid users forced the ELF specification to be changed.  It now
     says that the dynamic loader is responsible for determining the
     order in which the constructors have to run.  The constructors
     for all dependencies of an object must run before the constructor
     for the object itself.  Circular dependencies are left unspecified.

     This is highly questionable since it puts the burden on the dynamic
     loader which has to find the dependencies at runtime instead of
     letting the user do it right.  Stupidity rules!  */

  /* The host namespace first.  Its objects are the payload's dependencies as far as the payload is
     concerned, and a dependency's constructors run before its dependent's -- but the walk below
     cannot reach them.  What it has for a host library is the proxy, and a proxy has no constructors
     to run; the object with the constructors is next door, in a list this one never visits.

     Each object's own searchlist gives the order, walked backwards so that what a thing depends on is
     constructed before the thing itself.  call_init has already refused proxies and anything already
     constructed, so the overlap between one closure and another costs nothing.  */
  /* Everything the payload depends on, deepest first, but not the payload itself: it is first in the
     search list and so last here, and the host namespace has to be constructed before it.  */
  i = main_map->l_searchlist.r_nlist;
  while (i-- > 1)
    call_init (main_map->l_initfini[i], argc, argv, env);

#ifdef SHARED
  /* Then the host namespace, between the two.

     After the bundled libraries, because host code calls into them -- a constructor in libGL reaching
     strdup wants a libc that has finished starting.  Before the payload, because as far as it is
     concerned these are its dependencies, and a dependency is constructed first.

     The walk above cannot reach them: what it has for a host library is the proxy, which has no
     constructors, while the object that has them is next door in a list this one never visits.  Each
     object's own search list gives the order, backwards so that what a thing depends on goes first.
     call_init refuses proxies and anything already done, so overlapping closures cost nothing.  */
  Lmid_t host = _dl_minst_host_namespace_id ();
  if (__glibc_unlikely (host != LM_ID_BASE))
    for (struct link_map *l = GL(dl_ns)[host]._ns_loaded; l != NULL; l = l->l_next)
      {
	if (l->l_proxy)
	  continue;

	/* l_initfini, not l_searchlist.  The search list is the breadth-first walk of the
	   dependencies and is what symbol lookup wants; reversing it is not dependency order and
	   puts a library before something it depends on.  l_initfini is the topological sort glibc
	   builds for exactly this, and is what the loop below uses for the payload.

	   libGLX ran before libGLdispatch, which it needs, and called into it before its
	   constructor had set anything up.  */
	for (unsigned int j = l->l_searchlist.r_nlist; j-- > 0; )
	  call_init (l->l_initfini[j]->l_real, argc, argv, env);
      }
#endif

  /* And the payload last of all.  */
  if (main_map->l_searchlist.r_nlist > 0)
    call_init (main_map->l_initfini[0], argc, argv, env);

#ifndef HAVE_INLINED_SYSCALLS
  /* Finished starting up.  */
  _dl_starting_up = 0;
#endif
}
