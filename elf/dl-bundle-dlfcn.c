/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* The dlfcn surface, answered by the linker.
 *
 * glibc's dlopen reaches _dl_open, which is the loader entire -- link maps, scopes, namespaces, the
 * search path, dependency sorting. A payload has none of that here: the linker did the loading and
 * keeps the only record of what is loaded. So these forward.
 *
 * Through GLRO (dl_dlfcn_hook) rather than by replacing _dl_open, because glibc already has a
 * supported way to say "someone else implements this". Every entry point in dlfcn/ tests the hook
 * before doing anything else, so setting it once redirects the whole surface, and the code that gets
 * bypassed is bypassed rather than edited.
 *
 * The hook is what static dlopen uses for the same reason -- an outer implementation answering for
 * an inner libc -- which is why the structure has the internal libc_dl* entries as well as the
 * public ones. nss and gconv reach those.  */

#include <bundle/RuntimeInterface.h>

#include <dlfcn.h>
#include <ldsodefs.h>
#include <stddef.h>
#include <string.h>

/* RTLD_DL_SYMENT and RTLD_DL_LINKMAP, which dladdr1 selects between.  */
#include <bits/dlfcn.h>

/* The flags the linker accepts, which are the ones POSIX names and nothing else.

   glibc carries private bits in the same word -- __RTLD_DLOPEN above all, which every internal
   __libc_dlopen sets -- and the linker refuses a mode holding anything it does not recognise rather
   than ignoring it. That refusal is right: a flag it cannot honour is a request it cannot answer, and
   silently dropping one is how a caller ends up with a library loaded on terms it did not ask for.

   So they are dropped here, where what each one means is known. __RTLD_AUDIT, __RTLD_SECURE and
   __RTLD_NOIFUNC name machinery this loader does not have.

   __RTLD_DLOPEN is worth naming exactly, because it is not nothing. It says the load came from a
   dlopen call rather than from startup or dependency resolution, and glibc hangs two refusals on
   that: an object marked DF_1_NOOPEN may be loaded as a dependency and must be refused when
   dlopen'd, and a module wanting an executable stack may only be given one during startup. The
   linker enforces neither -- it has no DF_1_NOOPEN check and no stack handling -- so nothing is lost
   by dropping the bit today.

   What is lost is the ability to add either later, because after this the linker cannot tell the two
   kinds of load apart. If that is ever wanted, the distinction should arrive as an argument to
   OpenLibrary rather than as a flag smuggled through a word the linker validates -- the interface is
   ours, and a caller saying why it is loading is clearer than a bit it has to know to preserve.

   RTLD_DEEPBIND is dropped too and is not the same kind of thing: it asks for the library's own
   symbols to be preferred over the global scope, and the linker has no equivalent. Dropping it loses
   that preference rather than the load, which is the better of the two failures -- and an artifact
   that resolves out of itself is already most of what deep binding is asked for.

   Without this, every conversion iconv needs a module for is unavailable: gconv reaches its modules
   through __libc_dlopen, the mode carries __RTLD_DLOPEN, and the linker answers "invalid flags to
   dlopen" to a library it is perfectly able to load.  */
static int
bundle_open_flags (int mode)
{
  return mode & (RTLD_NOW | RTLD_LAZY | RTLD_NOLOAD | RTLD_GLOBAL | RTLD_NODELETE);
}

static void *
bundle_dlopen (const char *file, int mode, void *dl_caller)
{
  (void) dl_caller;

  return GL (dl_bundle_runtime)->OpenLibrary (file, bundle_open_flags (mode));
}

static int
bundle_dlclose (void *handle)
{
  return GL (dl_bundle_runtime)->CloseLibrary (handle);
}

static void *
bundle_dlsym (void *handle, const char *name, void *dl_caller)
{
  /* No version, which is not the same as the default version: it is no constraint at all, which is
     what an unversioned lookup means.  */
  return GL (dl_bundle_runtime)->FindSymbol (handle, name, NULL, dl_caller);
}

static void *
bundle_dlvsym (void *handle, const char *name, const char *version, void *dl_caller)
{
  return GL (dl_bundle_runtime)->FindSymbol (handle, name, version, dl_caller);
}

static char *
bundle_dlerror (void)
{
  /* Cast away const. dlerror's signature has always handed out a mutable pointer to a string the
     caller does not own and must not write, and that is not this file's to fix.  */
  return (char *) GL (dl_bundle_runtime)->LastError ();
}

static int
bundle_dladdr (const void *address, Dl_info *info)
{
  return GL (dl_bundle_runtime)->DescribeAddress (address, info, NULL);
}

static int
bundle_dladdr1 (const void *address, Dl_info *info, void **extra_info, int flags)
{
  const void *symbol_entry = NULL;
  int found = GL (dl_bundle_runtime)->DescribeAddress (address, info, &symbol_entry);

  if (found == 0 || flags == 0)
    return found;

  if (flags == RTLD_DL_SYMENT)
    {
      *extra_info = (void *) symbol_entry;

      return found;
    }

  if (flags == RTLD_DL_LINKMAP)
    {
      struct dl_find_object object;

      if (GL (dl_bundle_runtime)->DescribeObject (address, &object) != 0)
	return 0;

      *extra_info = object.dlfo_link_map;

      return found;
    }

  return 0;
}

/* dlmopen, with one libc.
 *
 * The id is a namespace index, which is what RTLD_DI_LMID answers with, so one obtained from dlinfo
 * comes back here meaning the same thing.  LM_ID_BASE is zero and the default namespace is index
 * zero, so those agree without a mapping; LM_ID_NEWLM is -1 and makes a namespace.
 *
 * What a caller gets is symbol isolation without a second libc, which is a departure from stock
 * glibc and the better semantics rather than a reduced one.  There a namespace loads its own copy of
 * everything it needs, libc included -- dl_open_worker calls __libc_early_init with `initial' false
 * for the inner one -- so the two halves of a process end up with two malloc arenas, two ctype
 * tables and two sets of stdio.  A plugin that returns an allocated string its host frees is then
 * corrupting a heap it does not belong to, which is most of what the manual suggests dlmopen for.
 *
 * The isolation people reach for is of symbols: a plugin's foo not colliding with another plugin's,
 * a plugin binding a different version of a library than the host has.  Duplicating libc is not that
 * -- it is a consequence of how glibc gets there.  */
static void *
bundle_dlmopen (Lmid_t nsid, const char *file, int mode, void *dl_caller)
{
  (void) dl_caller;

  const RuntimeInterface *runtime = GL (dl_bundle_runtime);

  if (runtime->OpenLibraryInNamespace == NULL)
    return NULL;

  return runtime->OpenLibraryInNamespace (file, mode, (int64_t) nsid);
}

/* Every request dlinfo has, answered from what the loader knows about the module.
 *
 * None of this is anything libc needs. dlinfo is a public interface, and every request is the
 * program asking the loader about an object it holds a handle to; libc's part is to pass the
 * question on. In stock glibc the two halves share an address space and reach the same structures,
 * so that distinction never has to be drawn -- here it is the whole shape of the thing.  */
static int
bundle_dlinfo (void *handle, int request, void *arg)
{
  /* RTLD_DI_SERINFO and RTLD_DI_SERINFOSIZE report the library search path, and a sealed payload
     does not have one. That is the point of it rather than a gap in it, so the honest answer is an
     empty set: no directories, and the size of a Dl_serinfo carrying none.
     An error here would say "cannot tell", which is a different and untrue thing, and would invite
     somebody to complete dlinfo later by inventing a search path -- which would quietly undo
     sealing.  */
  if (request == RTLD_DI_SERINFO || request == RTLD_DI_SERINFOSIZE)
    {
      Dl_serinfo *info = arg;

      info->dls_size = sizeof (Dl_serinfo);
      info->dls_cnt = 0;

      return 0;
    }

  BundleModuleInfo module;

  if (GL (dl_bundle_runtime)->DescribeModule (handle, &module) != 0)
    return -1;

  switch (request)
    {
    case RTLD_DI_LMID:
      *(Lmid_t *) arg = module.NamespaceIndex;
      return 0;

    case RTLD_DI_LINKMAP:
      /* Only the five fields of the public struct link_map may be read from this. Anything further
	 in sits at an offset only one build of glibc knows, and the loader is not that build.  */
      *(const void **) arg = module.LinkMap;
      return 0;

    case RTLD_DI_PHDR:
      *(const ElfW(Phdr) **) arg = module.ProgramHeaders;
      return module.ProgramHeaderCount;

    case RTLD_DI_TLS_MODID:
      *(size_t *) arg = module.TlsModuleId;
      return 0;

    case RTLD_DI_TLS_DATA:
      /* This thread's copy, allocated on first touch. Null for a module with no thread-local storage
	 -- and glibc reports null for a module this thread has not touched yet, which is the same
	 answer for the same reason.  */
      *(void **) arg = module.TlsModuleId == 0
	? NULL
	: GL (dl_bundle_runtime)->ThreadLocalAddress (module.TlsModuleId, 0);
      return 0;

    case RTLD_DI_ORIGIN:
    case RTLD_DI_ORIGIN_PATH:
      {
	/* The directory the module was loaded from, which is what $ORIGIN expands against.
	   A path in the bundle's filesystem view rather than on the machine, and that is the useful
	   answer rather than a stand-in for one: bundlefs serves that path, so a program that builds
	   $ORIGIN/../share to find its own data gets what the bundler put there.
	   No buffer length is passed. That is the interface's own flaw -- glibc trusts the caller to
	   have supplied enough -- and it is mirrored rather than corrected, because a caller written
	   against glibc sizes the buffer the way glibc requires.  */
	const char *path = module.Path;
	const char *last_slash = NULL;
	char *out = arg;

	for (const char *at = path; *at != '\0'; ++at)
	  if (*at == '/')
	    last_slash = at;

	if (last_slash == NULL)
	  {
	    out[0] = '.';
	    out[1] = '\0';

	    return 0;
	  }

	/* A module at the root keeps the slash, so that the origin of /libfoo.so is / and not the
	   empty string.  */
	size_t length = last_slash == path ? 1 : (size_t) (last_slash - path);

	memcpy (out, path, length);
	out[length] = '\0';

	return 0;
      }

    default:
      return -1;
    }
}

/* What libc itself calls, from nss and gconv. Same answers, without the caller address -- these ask
   on the process's behalf rather than on a caller's, so the search starts at the beginning.  */
static void *
bundle_libc_dlopen_mode (const char *name, int mode)
{
  /* This is the one that matters for the flags: every caller here is inside libc, and libc always
     sets __RTLD_DLOPEN.  */
  return GL (dl_bundle_runtime)->OpenLibrary (name, bundle_open_flags (mode));
}

static void *
bundle_libc_dlsym (void *map, const char *name)
{
  return GL (dl_bundle_runtime)->FindSymbol (map, name, NULL, NULL);
}

static void *
bundle_libc_dlvsym (void *map, const char *name, const char *version)
{
  return GL (dl_bundle_runtime)->FindSymbol (map, name, version, NULL);
}

static int
bundle_libc_dlclose (void *map)
{
  return GL (dl_bundle_runtime)->CloseLibrary (map);
}

static const struct dlfcn_hook _bundle_dlfcn_hook = {
  .dlopen = bundle_dlopen,
  .dlclose = bundle_dlclose,
  .dlsym = bundle_dlsym,
  .dlvsym = bundle_dlvsym,
  .dlerror = bundle_dlerror,
  .dladdr = bundle_dladdr,
  .dladdr1 = bundle_dladdr1,
  .dlinfo = bundle_dlinfo,
  .dlmopen = bundle_dlmopen,
  .libc_dlopen_mode = bundle_libc_dlopen_mode,
  .libc_dlsym = bundle_libc_dlsym,
  .libc_dlvsym = bundle_libc_dlvsym,
  .libc_dlclose = bundle_libc_dlclose,
};

/* Phase two, and it has to be: _dl_dlfcn_hook lives in _rtld_global_ro, which is read-only after
   relocation. The linker holds this object's RELRO open across InitializeLibc for the allocator
   pointers, and this rides in the same window.  */
void
__bundle_fill_dlfcn_hook (void)
{
  GLRO (dl_dlfcn_hook) = &_bundle_dlfcn_hook;
}
