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
#include <not-cancel.h>
#include <string.h>
#include <sys/mman.h>
#include <sysdep.h>
#include <unistd.h>

/* Loaders carry a handful of program headers; refusing past this is better than reading an unbounded
   table.  */
#define MAX_PHDRS 64

/* An artifact carries at most one bundle of each kind.  */
#define MAX_BUNDLES 2

struct minst_bundle
{
  const struct minst_bundle_header *header;
  const struct minst_bundle_entry *entries;
  const char *strings;
};

static int minst_fd = -1;
static struct minst_bundle minst_bundles[MAX_BUNDLES];
static size_t minst_nbundles;

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

const struct minst_bundle_view *
_dl_minst_bundle_view (void)
{
  if (minst_fd < 0)
    return NULL;

  minst_view.descriptor = minst_fd;

  return &minst_view;
}
rtld_hidden_def (_dl_minst_bundle_view)

/* Taken from the payload bundle, which is the one describing the artifact rather than the runtime
   inside it.  Zero when there is no artifact, so a loader running outside one behaves as it did.  */
static uint32_t
minst_flags (void)
{
  for (size_t b = 0; b < minst_nbundles; ++b)
    if (minst_bundles[b].header->type == MINST_BUNDLE_MAIN)
      return minst_bundles[b].header->flags;

  return 0;
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
  uint32_t seen = 0;

  for (size_t b = 0; b < minst_nbundles; ++b)
    {
      const struct minst_bundle *bundle = &minst_bundles[b];
      if (bundle->header->type != MINST_BUNDLE_RUNTIME)
	continue;

      for (uint32_t i = 0; i < bundle->header->count; ++i)
	{
	  const struct minst_bundle_entry *entry = &bundle->entries[i];
	  const char *member = bundle->strings + entry->name;

	  if ((entry->flags & MINST_ENTRY_PRELOAD) == 0)
	    continue;

	  if (seen++ != index)
	    continue;

	  *name = member;
	  return true;
	}
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

/* What has to be true before an index can be walked.  A malformed one is treated as not being ours
   rather than as an error: another note of the same number could belong to anyone, and only the
   owner and these bounds together say it is a bundle.  */
static bool
bundle_is_sound (const struct minst_bundle_header *header, uint32_t descsz)
{
  if (descsz < sizeof (*header))
    return false;
  if (header->magic != MINST_BUNDLE_MAGIC)
    return false;
  if (header->version != MINST_BUNDLE_VERSION)
    return false;
  if (header->libc != MINST_LIBC_GLIBC)
    return false;
  if (header->type != MINST_BUNDLE_MAIN && header->type != MINST_BUNDLE_RUNTIME)
    return false;
  if (header->strings < sizeof (*header) || header->strings > descsz)
    return false;
  if (header->count
      > (header->strings - sizeof (*header)) / sizeof (struct minst_bundle_entry))
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

      if (note->n_type != MINST_BUNDLE_NOTE_TYPE)
	continue;
      if (note->n_namesz != sizeof MINST_BUNDLE_OWNER)
	continue;
      if (memcmp (name, MINST_BUNDLE_OWNER, note->n_namesz) != 0)
	continue;

      const struct minst_bundle_header *header = (const void *) desc;
      if (!bundle_is_sound (header, note->n_descsz))
	continue;

      if (minst_nbundles == MAX_BUNDLES)
	return;

      struct minst_bundle *bundle = &minst_bundles[minst_nbundles++];
      bundle->header = header;
      bundle->entries = (const void *) (header + 1);
      bundle->strings = (const char *) header + header->strings;
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

  return minst_nbundles != 0;
}

bool
_dl_minst_open (const char *execfn)
{
  if (minst_fd != -1)
    return minst_nbundles != 0;

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
  for (size_t b = 0; b < minst_nbundles; ++b)
    {
      const struct minst_bundle *bundle = &minst_bundles[b];

      for (uint32_t i = 0; i < bundle->header->count; ++i)
	{
	  const struct minst_bundle_entry *entry = &bundle->entries[i];

	  /* The starting member of a bundle is what that bundle exists to run -- the program in the
	     payload, the loader in the runtime -- and neither is something a DT_NEEDED asks for.

	     The loader matters most.  libc.so.6 names it in DT_NEEDED, and that reference has to bind
	     to the loader already running rather than map a second copy of it out of the artifact.  */
	  if ((entry->flags & MINST_ENTRY_MAIN) != 0)
	    continue;

	  if (!names_member (name, bundle->strings + entry->name))
	    continue;

	  member->offset = entry->offset;
	  member->size = entry->size;
	  member->unique = (bundle->header->type == MINST_BUNDLE_RUNTIME);
	  return true;
	}
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

bool
_dl_minst_main (struct minst_member *member)
{
  for (size_t b = 0; b < minst_nbundles; ++b)
    {
      const struct minst_bundle *bundle = &minst_bundles[b];
      if (bundle->header->type != MINST_BUNDLE_MAIN)
	continue;

      for (uint32_t i = 0; i < bundle->header->count; ++i)
	{
	  const struct minst_bundle_entry *entry = &bundle->entries[i];
	  if ((entry->flags & MINST_ENTRY_MAIN) == 0)
	    continue;

	  member->offset = entry->offset;
	  member->size = entry->size;
	  /* The program, which is loaded once into the base namespace and is nothing's dependency.  */
	  member->unique = false;
	  return true;
	}
    }

  return false;
}
