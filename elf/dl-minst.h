/* Reading the bundle an artifact carries.
   Copyright (c) 2026, Jake Helfert

   This file is part of the GNU C Library as modified for minst.

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

#ifndef _DL_MINST_H
#define _DL_MINST_H 1

#include <link.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* Where a member sits inside the artifact.  The offset is page aligned, which is what lets a
   member's segments be mapped straight out of the containing file rather than copied: ELF requires
   every PT_LOAD to satisfy p_offset congruent to p_vaddr modulo the page size, and a page aligned
   base preserves that for every segment inside it.  */
struct minst_member
{
  off_t offset;
  size_t size;
  /* The member belongs to the runtime bundle, so the process must hold exactly one instance of it
     however many namespaces there are.  A second copy of libc is a second malloc arena, a second
     errno and a second set of locale state, and a pointer allocated on one side cannot be freed on
     the other.

     This is what DT_GNU_FLAGS_1/DF_GNU_1_UNIQUE was invented to record.  An artifact does not need
     the tag: which bundle a member came from says the same thing, and says it from the index rather
     than from the file.  */
  bool unique;
};

/* Finds the artifact and reads the bundles it carries.

   Unlike the musl runtime, this loader is not the artifact's outer file: stage zero is, and it
   rewrites AT_PHDR to describe the loader it mapped.  So the index cannot be reached through our own
   program headers the way musl reaches it -- it lives out in the artifact, and the artifact has to
   be opened to see it.  That descriptor is wanted regardless, since every member is mapped from it.

   EXECFN is AT_EXECFN, the path the kernel was given, which stage zero passes through untouched.
   /proc/self/exe covers what it cannot: a path since replaced, or an execution that supplied no
   usable name.

   Returns false if this file carries no bundle, which for a runtime with nowhere else to look is
   the end of the matter.  */
extern bool _dl_minst_open (const char *execfn) attribute_hidden;

/* The artifact, open for the life of the process: a dlopen may ask for a member at any time.  */
extern int _dl_minst_fd (void) attribute_hidden;

/* Whether nothing may resolve outside the artifact.  False when there is no artifact at all, and
   false for one that delegates part of its work to the host -- the graphics stack cannot be bundled,
   and its dependency closure cannot even be enumerated ahead of time.  */
extern bool _dl_minst_sealed (void) attribute_hidden;

/* Whether to trace what the loader does to stderr.  The environment cannot ask for this -- a loader
   that ignores the environment ignores LD_DEBUG with it -- so the artifact carries the switch, and an
   artifact that misbehaves somewhere unreproducible can be rebuilt with it and run there.  */
extern bool _dl_minst_trace (void) attribute_hidden;

/* Whether LD_PRELOAD, LD_LIBRARY_PATH and GLIBC_TUNABLES should be emptied in the environment.
   Nothing here reads them either way; this is about what is left for the payload and for whatever
   the payload starts, which is a separate question with a separate answer.  */
extern bool _dl_minst_blank_env (void) attribute_hidden;

/* Looks a member up by the name something asked for.  Both bundles are searched, because a payload
   library reaching libc is asking the runtime bundle for it.  */
extern bool _dl_minst_find (const char *name, struct minst_member *member) attribute_hidden;

/* The payload bundle's starting member: the program this artifact exists to run.  It is found by the
   flag that marks it, not by a name -- what the program is called is argv[0]'s business, and nothing
   here manufactures an identity for it out of the index.  */
extern bool _dl_minst_main (struct minst_member *member) attribute_hidden;

/* Maps a member that nothing asks for by name, which is how the program is loaded.  Declared here
   rather than in dl-load.h because it exists only for artifacts; it is defined next to the rest of
   the mapping machinery in dl-load.c, where the filebuf and open_verify live.  */
extern struct link_map *_dl_map_object_from_bundle (ElfW(Off) base_off, int type, int mode,
						    Lmid_t nsid) attribute_hidden;

/* The namespace host objects are loaded into, made on first use.  Only meaningful for an artifact
   that may reach the machine at all; a sealed one never asks.  */
extern Lmid_t _dl_minst_host_namespace (void) attribute_hidden;

/* Whether NAME has to come off the machine, and so belongs in the host namespace rather than beside
   the payload.  False for everything when the artifact is sealed, since then nothing does.  */
extern bool _dl_minst_belongs_to_host (const char *name);
rtld_hidden_proto (_dl_minst_belongs_to_host)

/* Gives everything placed in the host namespace its dependency closure, which the payload's own walk
   cannot do because what it sees of a host library is a proxy with nothing to walk.  Called once the
   payload's closure is complete and before anything is relocated.  */
extern void _dl_minst_finish_host_namespace (void) attribute_hidden;

/* Opens a data member as though it were a file of its own, or -1 when there is no such member.

   The member's bytes are copied into an anonymous file rather than the artifact being handed over at
   an offset.  What reads these -- gconv's configuration parser, the locale archive loader -- does its
   own arithmetic from the start of what it is given, and teaching two subsystems to work at an offset
   is far more surface than copying a configuration file.  Nothing is copied until something asks.  */
extern int _dl_minst_open_member (const char *name) attribute_hidden;

#endif /* dl-minst.h */
