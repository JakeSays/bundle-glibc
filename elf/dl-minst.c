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

#include <dl-minst.h>

#include <dl-minst-bundle.h>
#include <elf.h>
#include <ldsodefs.h>
#include <link.h>
#include <minst/minst_bundle.h>
#include <minst/minst_view.h>
#include <not-cancel.h>
#include <string.h>
#include <sys/mman.h>
#include <sysdep.h>
#include <unistd.h>

/* Loaders carry a handful of program headers; refusing past this is better than reading an unbounded
   table.  */
#define MAX_PHDRS 64

static int minst_fd = -1;

/* The view note, which says where everything is.
 *
 * It replaces the member index above.  Both are read because the bundler can still write either, and
 * an artifact built by whichever end has to start; the index goes when nothing writes one.
 *
 * What it buys is that a member is a file inside an image rather than a section of its own, and this
 * loader cannot read that filesystem.  bundlefs comes up inside __libc_early_init, which cannot run
 * until libc is mapped, which is this.  So every object's extent is recorded here and the loader maps
 * an offset exactly as it did before.  */
static const struct minst_view_header *minst_note;
static const struct minst_view_soname *minst_note_sonames;
static const char *minst_note_strings;

/* Stage zero's dynamic array, which exists only to carry DT_DEBUG.  Its address is taken straight
   from the program header because stage zero is not position independent, so what it was linked at
   is where it is.  NULL when the artifact was built before stage zero carried one, and then nothing
   below happens.  */
static ElfW(Dyn) *minst_stage0_dynamic;

int
_dl_minst_fd (void)
{
  return minst_fd;
}

/* Handed to libc so it can bring the bundle filesystem up against the artifact this process is
   running out of.  Filled in once, when there is an artifact at all, and read from libc afterwards.

   The image list is empty: nothing writes an EROFS image into an artifact yet.  libc treats that as
   an artifact with nothing to serve, which is what it is.  */
static struct minst_bundle_view minst_view;

/* One per mount rather than one per image: what libc does with these is call
   BfsFileSystemMount, which wants an image and the pair of paths, and an image mounted twice is two
   mounts of one image.  Deeper than any manifest anyone means to write, and a fixed count so nothing
   here allocates -- this runs before libc has a heap.  */
#define MAX_MOUNTS 64

static struct minst_bundle_image minst_mounts[MAX_MOUNTS];

const struct minst_bundle_view *
_dl_minst_bundle_view (void)
{
  if (minst_fd < 0 || minst_note == NULL)
    return NULL;

  minst_view.descriptor = minst_fd;

  if (minst_view.images == NULL)
    {
      const struct minst_view_image *images
	= (const void *) ((const unsigned char *) minst_note + minst_note->image_offset);
      const struct minst_view_mount *mounts
	= (const void *) ((const unsigned char *) minst_note + minst_note->mount_offset);

      uint32_t taken = 0;

      for (uint32_t i = 0; i < minst_note->mount_count && taken < MAX_MOUNTS; ++i)
	{
	  if (mounts[i].image >= minst_note->image_count)
	    continue;

	  const struct minst_view_image *image = &images[mounts[i].image];

	  minst_mounts[taken].offset = image->offset;
	  minst_mounts[taken].length = image->size;
	  minst_mounts[taken].mount_point = minst_note_strings + mounts[i].mount_point;
	  minst_mounts[taken].image_path = minst_note_strings + mounts[i].image_path;
	  ++taken;
	}

      minst_view.images = minst_mounts;
      minst_view.image_count = taken;

      /* Handed over as they sit in the note. Nothing here has to be resolved against an image the
	 way a mount does, so there is nothing to copy and no reason to bound how many there may
	 be.  */
      if (minst_note->environment_count != 0)
	{
	  minst_view.environment
	    = (const void *) ((const unsigned char *) minst_note
			      + minst_note->environment_offset);
	  minst_view.environment_count = minst_note->environment_count;
	  minst_view.strings = minst_note_strings;
	}
    }

  return &minst_view;
}
rtld_hidden_def (_dl_minst_bundle_view)

/* What the artifact may do.  Zero when there is no artifact, so a loader running outside one behaves
   as it did.  */
static uint32_t
minst_flags (void)
{
  return minst_note != NULL ? minst_note->flags : 0;
}

bool
_dl_minst_sealed (void)
{
  return (minst_flags () & MINST_BUNDLE_SEALED) != 0;
}

bool
_dl_minst_blank_env (void)
{
  return (minst_flags () & MINST_BUNDLE_BLANK_ENV) != 0;
}

bool
_dl_minst_trace (void)
{
  return (minst_flags () & MINST_BUNDLE_TRACE) != 0;
}

bool
_dl_minst_runtime_library (uint32_t index, const char **name)
{
  if (minst_note == NULL)
    return false;

  uint32_t seen = 0;

  for (uint32_t i = 0; i < minst_note->soname_count; ++i)
    {
      const struct minst_view_soname *entry = &minst_note_sonames[i];

      if ((entry->flags & MINST_SONAME_PRELOAD) == 0)
	continue;

      if (seen++ != index)
	continue;

      *name = minst_note_strings + entry->name;
      return true;
    }

  return false;
}

/* Reads exactly LENGTH bytes, or says it could not.  Every caller here wants a whole header or a
   whole table; a short read of one is not something to carry on from.  */
static bool
read_exactly (void *destination, size_t length, off_t offset)
{
  ssize_t got = __pread64_nocancel (minst_fd, destination, length, offset);
  return got >= 0 && (size_t) got == length;
}

/* What has to be true before the view can be walked.  Another note of the same number could belong to
   anyone, so the owner and these together are what say it is ours, and every count is checked against
   the space actually there.  */
static bool
view_is_sound (const struct minst_view_header *header, uint32_t descsz)
{
  if (descsz < sizeof (*header))
    return false;
  if (header->magic != MINST_VIEW_MAGIC)
    return false;
  if (header->version != MINST_VIEW_VERSION)
    return false;
  if (header->string_offset > descsz
      || header->string_size > descsz - header->string_offset)
    return false;
  if (header->soname_offset > descsz)
    return false;
  if (header->soname_count
      > (descsz - header->soname_offset) / sizeof (struct minst_view_soname))
    return false;
  if (header->environment_offset > descsz)
    return false;
  if (header->environment_count
      > (descsz - header->environment_offset) / sizeof (struct minst_view_environment))
    return false;
  return true;
}

/* Walks one PT_NOTE's contents for bundles.  The region is already in memory; nothing here reads
   the file again.  */
static void
scan_notes (const unsigned char *cursor, const unsigned char *end)
{
  while (end - cursor >= (ptrdiff_t) sizeof (ElfW(Nhdr)))
    {
      const ElfW(Nhdr) *note = (const void *) cursor;
      const unsigned char *name = cursor + sizeof (*note);
      const unsigned char *desc = name + ((note->n_namesz + 3) & ~3u);

      cursor = desc + ((note->n_descsz + 3) & ~3u);
      if (cursor > end)
	return;

      if (note->n_namesz != sizeof MINST_BUNDLE_OWNER)
	continue;
      if (memcmp (name, MINST_BUNDLE_OWNER, note->n_namesz) != 0)
	continue;

      if (note->n_type != MINST_VIEW_NOTE_TYPE)
	continue;

      const struct minst_view_header *view = (const void *) desc;
      if (minst_note != NULL || !view_is_sound (view, note->n_descsz))
	continue;

      minst_note = view;
      minst_note_sonames
	= (const void *) ((const unsigned char *) view + view->soname_offset);
      minst_note_strings = (const char *) view + view->string_offset;
    }
}

/* The artifact's own program headers, read from the file.  Its notes are not in this process's
   memory: what the kernel mapped is stage zero, and what AT_PHDR now describes is this loader.  */
static bool
read_bundles (void)
{
  ElfW(Ehdr) header;
  if (!read_exactly (&header, sizeof (header), 0))
    return false;

  if (memcmp (header.e_ident, ELFMAG, SELFMAG) != 0)
    return false;
  if (header.e_phentsize != sizeof (ElfW(Phdr)))
    return false;
  if (header.e_phnum == 0 || header.e_phnum > MAX_PHDRS)
    return false;

  ElfW(Phdr) phdrs[MAX_PHDRS];
  size_t table = (size_t) header.e_phnum * header.e_phentsize;
  if (!read_exactly (phdrs, table, header.e_phoff))
    return false;

  size_t pagesize = GLRO(dl_pagesize);

  for (unsigned int i = 0; i < header.e_phnum; ++i)
    {
      if (phdrs[i].p_type == PT_DYNAMIC && phdrs[i].p_filesz != 0)
	{
	  minst_stage0_dynamic = (ElfW(Dyn) *) phdrs[i].p_vaddr;
	  continue;
	}

      if (phdrs[i].p_type != PT_NOTE || phdrs[i].p_filesz == 0)
	continue;

      /* Mapped rather than copied, and left mapped: the entries and the strings are referred to for
	 the life of the process, so the region has to outlive this function.  */
      off_t start = phdrs[i].p_offset & ~(off_t) (pagesize - 1);
      size_t slack = (size_t) (phdrs[i].p_offset - start);
      size_t length = slack + phdrs[i].p_filesz;

      void *mapped = __mmap (NULL, length, PROT_READ, MAP_PRIVATE, minst_fd, start);
      if (mapped == MAP_FAILED)
	continue;

      const unsigned char *notes = (const unsigned char *) mapped + slack;
      scan_notes (notes, notes + phdrs[i].p_filesz);
    }

  return minst_note != NULL;
}

bool
_dl_minst_open (const char *execfn)
{
  if (minst_fd != -1)
    return minst_note != NULL;

  if (execfn != NULL)
    minst_fd = __open_nocancel (execfn, O_RDONLY | O_CLOEXEC);

  if (minst_fd < 0)
    minst_fd = __open_nocancel ("/proc/self/exe", O_RDONLY | O_CLOEXEC);

  if (minst_fd < 0)
    return false;

  if (!read_bundles ())
    {
      __close_nocancel (minst_fd);
      minst_fd = -1;
      return false;
    }

  return true;
}

/* Whether a name glibc built a path for names this member.

   Most things are asked for by the name they are carried under, and match outright.  gconv is the
   exception: glibc joins its compiled-in module directory to a name from the configuration and opens
   the result, so what arrives is "/usr/lib64/gconv/UTF-16.so" while the member is "gconv/UTF-16.so".
   That directory describes a machine this artifact is not running on, and the part that identifies
   the module is the tail.

   The match is on a component boundary, so "gconv/UTF-16.so" is reached by ".../gconv/UTF-16.so" and
   never by ".../notgconv/UTF-16.so".  */
static bool
names_member (const char *asked, const char *carried)
{
  if (strcmp (asked, carried) == 0)
    return true;

  size_t asked_len = strlen (asked);
  size_t carried_len = strlen (carried);

  if (asked_len <= carried_len)
    return false;
  if (asked[asked_len - carried_len - 1] != '/')
    return false;

  return strcmp (asked + asked_len - carried_len, carried) == 0;
}

bool
_dl_minst_find (const char *name, struct minst_member *member)
{
  if (minst_note != NULL)
    {
      for (uint32_t i = 0; i < minst_note->soname_count; ++i)
	{
	  const struct minst_view_soname *entry = &minst_note_sonames[i];

	  /* Nothing to map, which is a compressed object: it has no contiguous layout, and this
	     loader has nothing to decompress it with.  Passed over rather than failed here, so the
	     complaint comes from whatever actually wanted it.  */
	  if (entry->size == 0)
	    continue;

	  /* The machine's copy is the one to use, so this is not a member to map at all.  */
	  if ((entry->flags & MINST_SONAME_HOST) != 0)
	    continue;

	  if (!names_member (name, minst_note_strings + entry->name))
	    continue;

	  member->offset = entry->offset;
	  member->size = entry->size;

	  /* There may be only one of anything a referenced unit supplies.  Image zero is what this
	     artifact was built from; anything else arrived as a unit, and a libc is what that means in
	     practice -- which is the same thing the runtime bundle said before.  */
	  member->unique = (entry->image != 0);
	  return true;
	}

      return false;
    }

  return false;
}

void
_dl_minst_publish_rendezvous (ElfW(Addr) address)
{
  if (minst_stage0_dynamic == NULL)
    return;

  /* A debugger reads DT_DEBUG out of the executable's dynamic segment to find the rendezvous
     structure, and from there the chain of everything loaded.  Ordinarily the loader fills that in
     on the program it started; here the program the kernel started is stage zero, which has no
     dynamic segment of its own except the one it carries for exactly this.  Without it a debugger
     concludes that nothing is loaded and every frame is unresolvable.  */
  for (ElfW(Dyn) *entry = minst_stage0_dynamic; entry->d_tag != DT_NULL; ++entry)
    if (entry->d_tag == DT_DEBUG)
      {
	entry->d_un.d_ptr = address;
	return;
      }
}

int
_dl_minst_open_member (const char *name)
{
  struct minst_member member;
  if (!_dl_minst_find (name, &member))
    return -1;

  /* The syscall directly: memfd_create has no internal alias, and this runs in the loader where the
     public one is not available.  */
  int fd = INLINE_SYSCALL_CALL (memfd_create, name, MFD_CLOEXEC);
  if (fd < 0)
    return -1;

  off_t from = member.offset;
  off_t to = 0;
  size_t left = member.size;

  /* Positioned reads and writes throughout, so the descriptor is handed over still at zero.  What
     receives it expects a file nobody has touched, and the artifact's own descriptor is shared with
     every other member and must not be moved.  */
  while (left > 0)
    {
      char buffer[4096];
      size_t want = left < sizeof (buffer) ? left : sizeof (buffer);

      ssize_t got = __pread64_nocancel (minst_fd, buffer, want, from);
      if (got <= 0)
	goto failed;

      ssize_t put = INLINE_SYSCALL_CALL (pwrite64, fd, buffer, got, to);
      if (put != got)
	goto failed;

      from += got;
      to += got;
      left -= (size_t) got;
    }

  return fd;

failed:
  __close_nocancel (fd);
  return -1;
}

/* Any proxy still standing for MAP at the moment MAP is freed.
 *
 * A proxy is a shell around another namespace's object: it copies the mapping but shares the name and
 * the name list, which is what makes _dl_name_match_p answer the same for both. Sharing means the
 * proxy holds no reference -- freeing the object frees the strings out from under it, and the next
 * walk of the proxy's namespace reads a name pointer into memory the allocator has handed out again.
 * The proxy itself is untouched, so nothing marks it dead and every structural check passes.  */
void
_dl_minst_check_proxies (struct link_map *map)
{
  if (__glibc_likely ((GLRO(dl_debug_mask) & DL_DEBUG_FILES) == 0))
    return;

  for (Lmid_t ns = 0; ns < DL_NNS; ++ns)
    for (struct link_map *l = GL(dl_ns)[ns]._ns_loaded; l != NULL; l = l->l_next)
      if (l->l_proxy && l->l_real == map)
	_dl_debug_printf ("minst: freeing %s [%lu] at %lx, which the proxy at %lx in namespace %lu"
			  " stands for and takes its name from\n",
			  DSO_FILENAME (map->l_name), map->l_ns,
			  (unsigned long int) (uintptr_t) map,
			  (unsigned long int) (uintptr_t) l, (unsigned long int) ns);
}

/* Every search list in every namespace, checked against what the loader still has loaded.
 *
 * A map is reachable from its namespace's list for as long as it exists, so an entry that is not
 * reachable has been freed -- and a lookup walking that list will read a symbol table out of memory
 * the allocator has since given to somebody else.
 *
 * Diagnostic rather than a repair. It is called at the points that bracket the operations under
 * suspicion, and WHEN says which one it was, so that a list going bad can be attributed to something
 * narrower than "somewhere during startup". Quiet unless something is wrong, so leaving the calls in
 * costs a walk under the trace flag and nothing otherwise.  */
void
_dl_minst_check_scopes (const char *when)
{
  if (__glibc_likely ((GLRO(dl_debug_mask) & DL_DEBUG_FILES) == 0))
    return;

  unsigned int lists = 0;
  unsigned int entries = 0;
  unsigned int bad = 0;

  /* Whether any object is linked into two namespace lists.
     l_next holds one successor, so a map in two lists gives one of them a chain that belongs to the
     other -- and a walk following it leaves the list entirely and reads whatever is there. That is
     what a crash in an unrelated heap block looks like when nothing has been freed.
     Bounded as well: a chain that does not end is the same fault seen from the other side.  */
  for (Lmid_t ns = 0; ns < DL_NNS; ++ns)
    {
      unsigned int walked = 0;

      for (struct link_map *l = GL(dl_ns)[ns]._ns_loaded; l != NULL; l = l->l_next)
	{
	  if (++walked > 4096)
	    {
	      ++bad;
	      _dl_debug_printf ("minst: %s: namespace %lu chain does not end\n",
				when, (unsigned long int) ns);
	      break;
	    }

	  for (Lmid_t other = ns + 1; other < DL_NNS; ++other)
	    {
	      unsigned int scanned = 0;

	      for (struct link_map *o = GL(dl_ns)[other]._ns_loaded;
		   o != NULL && ++scanned <= 4096; o = o->l_next)
		if (o == l)
		  {
		    ++bad;
		    _dl_debug_printf ("minst: %s: object %lx (%s) is linked into namespace %lu"
				      " and namespace %lu\n",
				      when, (unsigned long int) (uintptr_t) l,
				      DSO_FILENAME (l->l_name),
				      (unsigned long int) ns, (unsigned long int) other);
		    break;
		  }
	    }
	}
    }

  /* The namespace lists themselves, first.
     Everything below is checked by asking whether an object is reachable from one of these, which
     answers nothing if the list has a freed object in it -- and a freed object still linked is what
     _dl_lookup_map walks when it compares names, reading a name pointer out of reused memory.
     A map records the namespace it belongs to, so a map found in a list that disagrees is not one.  */
  for (Lmid_t ns = 0; ns < DL_NNS; ++ns)
    {
      unsigned int position = 0;

      for (struct link_map *l = GL(dl_ns)[ns]._ns_loaded; l != NULL; l = l->l_next, ++position)
	if (l->l_ns == (Lmid_t) 0x6D696E73 || l->l_real == NULL
	    || l->l_ns != ns || (!l->l_proxy && l->l_real != l))
	  {
	    ++bad;
	    _dl_debug_printf ("minst: %s: namespace %lu holds a stale object at position %u,"
			      " address %lx (says ns %lu, real %lx)\n",
			      when, (unsigned long int) ns, position,
			      (unsigned long int) (uintptr_t) l,
			      l->l_ns, (unsigned long int) (uintptr_t) l->l_real);
	    break;
	  }
    }

  for (Lmid_t ns = 0; ns < DL_NNS; ++ns)
    for (struct link_map *map = GL(dl_ns)[ns]._ns_loaded; map != NULL; map = map->l_next)
      {
	if (map->l_searchlist.r_list == NULL)
	  continue;

	++lists;

	for (unsigned int i = 0; i < map->l_searchlist.r_nlist; ++i)
	  {
	    ++entries;
	    struct link_map *entry = map->l_searchlist.r_list[i];
	    bool loaded = false;

	    if (entry != NULL && (unsigned int) entry->l_ns < DL_NNS)
	      for (struct link_map *l = GL(dl_ns)[entry->l_ns]._ns_loaded;
		   l != NULL; l = l->l_next)
		if (l == entry)
		  {
		    loaded = true;
		    break;
		  }

	    if (!loaded)
	      {
		++bad;
		_dl_debug_printf ("minst: %s: search list of %s [%lu] entry %u at %lx"
				  " is not a loaded object\n",
				  when, DSO_FILENAME (map->l_name), map->l_ns, i,
				  (unsigned long int) (uintptr_t) entry);
	      }
	  }
      }

  /* And the namespace's own shared scope, which belongs to no object and so is not reached by the
     walk above. An object leaving the namespace is what would strand an entry here: the array is
     rebuilt only when something arrives.

     Shared only, as the scope itself is: a static libc has no loader and no second namespace.  */
#ifdef SHARED
  struct r_scope_elem *shared = _dl_minst_host_scope ();

  if (shared != NULL && shared->r_list != NULL)
    {
      ++lists;

      for (unsigned int i = 0; i < shared->r_nlist; ++i)
	{
	  struct link_map *entry = shared->r_list[i];
	  bool loaded = false;

	  ++entries;

	  if (entry != NULL && (unsigned int) entry->l_ns < DL_NNS)
	    for (struct link_map *l = GL(dl_ns)[entry->l_ns]._ns_loaded; l != NULL; l = l->l_next)
	      if (l == entry)
		{
		  loaded = true;
		  break;
		}

	  if (!loaded)
	    {
	      ++bad;
	      _dl_debug_printf ("minst: %s: the host namespace's shared scope entry %u at %lx"
				" is not a loaded object\n",
				when, i, (unsigned long int) (uintptr_t) entry);
	    }
	}
    }
#endif

  /* Always, so that a clean result is evidence the walk happened rather than evidence of nothing.
     A check that silently did not run looks exactly like a check that passed.  */
  _dl_debug_printf ("minst: %s: checked %u list(s), %u entr(ies), %u bad\n",
		    when, lists, entries, bad);
}

bool
_dl_minst_main (struct minst_member *member)
{
  if (minst_note == NULL || minst_note->program_size == 0)
    return false;

  member->offset = minst_note->program_offset;
  member->size = minst_note->program_size;
  /* The program, which is loaded once into the base namespace and is nothing's dependency.  */
  member->unique = false;
  return true;
}
