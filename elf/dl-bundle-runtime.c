/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* The handshake between the linker and this runtime.

   A new file rather than a modification of one, so it carries its own licence and not the tree's --
   the same reason dl-minst.c gave.

   The linker maps this object and prelinks it before anything else, looks this symbol up in the
   dynamic symbol table, and calls it.  Nothing has been relocated when that happens, which is what
   the whole arrangement rests on: the linker needs answers about thread-local layout before it can
   write an initial-exec relocation, and this runtime cannot run ordinary code until those
   relocations exist.  The handshake is the one point where each half can reach the other.

   Everything reachable from here therefore runs in an unrelocated image.  That permits arithmetic,
   string literals, calls to hidden functions, and reads and writes of static variables.  It rules
   out static variables holding addresses, calls through the PLT to anything interposable, IFUNC
   resolution, and errno -- which is thread-local, and so unavailable to the code deciding where
   thread-local storage goes.

   Taking the address of a hidden function compiles to a lea off the instruction pointer and needs no
   relocation, which is why filling a table of them is possible at all.  */

#include <bundle/RuntimeInterface.h>
#include <ldsodefs.h>
#include <rtld-malloc.h>

/* Where the auxiliary vector is turned into the values libc reads. A header of static inlines, so it
   compiles into this object and adds no call to anything that would need relocating.

   dl-auxv.h first, which is what supplies the architecture's DL_PLATFORM_AUXV. dl-sysdep.c orders
   them the same way for the same reason.  */
#include <dl-auxv.h>
#include <dl-parse_auxv.h>

/* For the HAVE_*_VSYSCALL names, which say which calls this architecture has in the vdso and what
   each one is called there.  */
#include <sysdep.h>

/* RTLD_NOW and RTLD_NOLOAD, for the handle the vdso lookups go through.  */
#include <dlfcn.h>

/* Filled in dl-bundle-tls.c, which is where the layout lives. Split because that file is about one
   thing and this one is about the handshake, not because they run at different times.  */
extern void __bundle_fill_tls_interface (RuntimeInterface *interface) attribute_hidden;

/* Points glibc's whole dlfcn surface at the linker. Phase two, because what it writes is in
   _rtld_global_ro -- see dl-bundle-dlfcn.c.  */
extern void __bundle_fill_dlfcn_hook (void) attribute_hidden;

/* What stock glibc reaches with _dl_lookup_direct on libc's map, from rtld.c, before _dl_init runs
   anything.  There is no map to look it up in here, and no need for one: libc.so exports it, and the
   linker resolves this object's undefined symbols against everything it loaded.

   Weak, and that is load-bearing rather than defensive.  This object is linked into ld.so, and ld.so
   is linked closed against libc_pic.a -- a strong undefined reference is satisfied there and now,
   pulling a second copy of libc_early_init.os and its allocator into ld.so.  The call would then
   reach ld.so's private copy rather than the one in the libc the process is actually running.  A
   weak undefined reference pulls no archive member, leaves the symbol for load time, and gets the
   right definition.  */
extern void __libc_early_init (_Bool initial) __attribute__ ((weak));

/* Points this half at libc's allocator, which is what __rtld_malloc_init_real does in stock glibc.

   Not that function, for two reasons that are both structural rather than awkward.  It takes the
   main map and reaches libc's symbols with the loader's own versioned lookup across its scopes,
   which is the machinery a payload's libc does not keep.  And it must run before RELRO closes,
   which in stock glibc is a moment during rtld's own startup and here is a moment the linker
   chooses -- it holds this object's RELRO open across InitializeLibc for exactly this write.

   Without it the four pointers stay aimed at the loader's bump allocator, whose free does nothing.
   _dl_deallocate_tls frees through them, so every thread that exited would leak its thread-local
   blocks and its control block.

   A null handle is the interface's RTLD_DEFAULT: search everything loaded, in load order.  libc is
   loaded by now -- this runs after every module is relocated -- so the lookups resolve.  */
static void
bundle_use_libc_allocator (void)
{
  const RuntimeInterface *runtime = GL (dl_bundle_runtime);

  void *new_calloc = runtime->FindSymbol (NULL, "calloc", NULL, &bundle_use_libc_allocator);
  void *new_free = runtime->FindSymbol (NULL, "free", NULL, &bundle_use_libc_allocator);
  void *new_malloc = runtime->FindSymbol (NULL, "malloc", NULL, &bundle_use_libc_allocator);
  void *new_realloc = runtime->FindSymbol (NULL, "realloc", NULL, &bundle_use_libc_allocator);

  /* All four or none.  A half-switched allocator frees with one implementation what the other
     allocated, which is worse than the leak this replaces.  */
  if (new_calloc == NULL || new_free == NULL || new_malloc == NULL || new_realloc == NULL)
    return;

  /* In one go, as stock glibc does here, so that anything allocating underneath the lookups above
     sees one implementation throughout.  */
  __rtld_calloc = new_calloc;
  __rtld_free = new_free;
  __rtld_malloc = new_malloc;
  __rtld_realloc = new_realloc;
}

/* The kernel's implementations of the handful of calls that only read kernel-maintained data.
   clock_gettime, gettimeofday, time, getcpu and clock_getres are in the vdso the kernel maps into
   every process, and calling them there stays in userspace instead of entering the kernel.

   Stock glibc fills these in setup_vdso_pointers, called from dl_main and from the static startup in
   dl-support.c.  Neither runs here, so without this every one of them is null and libc falls back to
   the syscall -- correct, and roughly an order of magnitude slower for something as short as reading
   a clock.

   setup_vdso is not needed to get there.  Its whole job is building a link_map for dl_vdso_vsym to
   search, which would mean _dl_new_object, _dl_add_to_namespace_list onto the chain this design keeps
   empty, and GLRO(dl_sysinfo_map).  The loader already holds the vdso as a prelinked object with its
   symbol table, and answering symbol questions is what it is for.

   Asked for by handle rather than against everything loaded, because the vdso does not answer to an
   unscoped lookup and should not.  It is loaded RTLD_LOCAL, so dlsym(RTLD_DEFAULT) passes over it --
   and stock glibc arrives at the same place from the other direction, giving the vdso a local scope
   containing only itself so that nothing resolves __vdso_ names by accident.  A NOLOAD open names the
   object the kernel already mapped without loading anything.

   The handle is not closed.  The vdso cannot be unloaded and holding a reference to something that
   cannot go away costs nothing, where closing would mean caring about the order this runs in relative
   to everything else that might hold one.

   LINUX_2.6 is the version every one of these carries; asking without it would take whichever
   definition an unversioned lookup found.  A null answer is the expected case for a kernel that does
   not have one -- __vdso_getrandom arrived in 6.11 -- and is what setup_vdso_pointers would leave
   there too.  */
static void
bundle_use_kernel_vdso (void)
{
  const RuntimeInterface *runtime = GL (dl_bundle_runtime);

  if (runtime->OpenLibrary == NULL || runtime->FindSymbol == NULL)
    return;

  void *vdso = runtime->OpenLibrary ("linux-vdso.so.1", RTLD_NOW | RTLD_NOLOAD);

  if (vdso == NULL)
    return;

#define BundleVdsoSymbol(name) \
  runtime->FindSymbol (vdso, (name), "LINUX_2.6", &bundle_use_kernel_vdso)

#ifdef HAVE_CLOCK_GETTIME_VSYSCALL
  GLRO(dl_vdso_clock_gettime) = BundleVdsoSymbol (HAVE_CLOCK_GETTIME_VSYSCALL);
#endif
#ifdef HAVE_CLOCK_GETTIME64_VSYSCALL
  GLRO(dl_vdso_clock_gettime64) = BundleVdsoSymbol (HAVE_CLOCK_GETTIME64_VSYSCALL);
#endif
#ifdef HAVE_GETTIMEOFDAY_VSYSCALL
  GLRO(dl_vdso_gettimeofday) = BundleVdsoSymbol (HAVE_GETTIMEOFDAY_VSYSCALL);
#endif
#ifdef HAVE_TIME_VSYSCALL
  GLRO(dl_vdso_time) = BundleVdsoSymbol (HAVE_TIME_VSYSCALL);
#endif
#ifdef HAVE_GETCPU_VSYSCALL
  GLRO(dl_vdso_getcpu) = BundleVdsoSymbol (HAVE_GETCPU_VSYSCALL);
#endif
#ifdef HAVE_CLOCK_GETRES_VSYSCALL
  GLRO(dl_vdso_clock_getres) = BundleVdsoSymbol (HAVE_CLOCK_GETRES_VSYSCALL);
#endif
#ifdef HAVE_CLOCK_GETRES64_VSYSCALL
  GLRO(dl_vdso_clock_getres_time64) = BundleVdsoSymbol (HAVE_CLOCK_GETRES64_VSYSCALL);
#endif
#ifdef HAVE_GET_TBFREQ
  GLRO(dl_vdso_get_tbfreq) = BundleVdsoSymbol (HAVE_GET_TBFREQ);
#endif
#ifdef HAVE_RISCV_HWPROBE
  GLRO(dl_vdso_riscv_hwprobe) = BundleVdsoSymbol (HAVE_RISCV_HWPROBE);
#endif
#ifdef HAVE_GETRANDOM_VSYSCALL
  GLRO(dl_vdso_getrandom) = BundleVdsoSymbol (HAVE_GETRANDOM_VSYSCALL);
#endif

#undef BundleVdsoSymbol
}

/* Wrapped rather than pointed at directly because the interface slot is filled during phase one.
   Storing __libc_early_init's address there would mean storing an address in another object, which
   is a relocation, and phase one has none.  The address of this function is a lea off the
   instruction pointer, and by the time it is called everything is relocated.  */
static void
bundle_initialize_libc (void)
{
  /* Before __libc_early_init, which allocates -- __ptmalloc_init and the bundle filesystem both do
     -- so that what it allocates comes from the allocator that can free it.  */
  bundle_use_libc_allocator ();

  /* Also before it, because __libc_early_init brings up the bundle filesystem and nss and gconv
     reach dlopen through this hook. Both writes are to areas the linker is holding open for exactly
     this call.  */
  __bundle_fill_dlfcn_hook ();

  /* Same window, same reason: these live in _rtld_global_ro alongside the allocator pointers above,
     and the linker holds this object's RELRO open across this call.  Before __libc_early_init, which
     reaches getrandom.  */
  bundle_use_kernel_vdso ();

  if (__libc_early_init == NULL)
    return;

  /* True because there is one namespace and this is its libc.  The argument distinguishes the base
     namespace's copy from one brought up later by dlmopen, which is a thing a sealed payload does
     not have.  */
  __libc_early_init (true);
}

void
__bundle_initialize_runtime (RuntimeInterface *interface)
{
  /* Ours goes in whatever we were handed, so a linker that refuses can name both numbers.  */
  uint32_t offered = interface->Size;

  interface->Version = BUNDLE_RUNTIME_INTERFACE_VERSION;
  interface->Size = sizeof (RuntimeInterface);

  /* A structure smaller than ours has no room for the fields below, and writing them would run off
     the end of the linker's.  Leaving them null is the only safe answer; the linker compares the two
     sizes and decides what to say.  */
  if (offered < sizeof (RuntimeInterface))
    return;

  /* Kept for the life of the process.  libc reaches everything the linker offers through this and
     resolves none of it by name -- see the comment on the field in ldsodefs.h for why it is one
     pointer rather than a function per thing wanted.

     A single store of a pointer we were handed, so nothing here needs relocating.  */
  GL (dl_bundle_runtime) = interface;

  /* Before anything else, because most of what follows reads one of these.

     rtld publishes these out of the auxiliary vector during its own startup, and that startup never
     runs here -- the linker maps this object and calls straight in. Leaving them zero is not a
     degraded mode: _dl_determine_tlsoffset sizes the restartable-sequence area by rounding up to
     _rseq_align, which is a division, and an unpublished _rseq_align is a divide by zero before the
     first line of the program.

     The vector rather than a copy of the values, so that whatever this release of glibc reads out of
     it is what it gets. A loader picking values to pass would be choosing on glibc's behalf, and the
     set changes between releases.  */
  if (interface->GetAuxiliaryVector != NULL)
    {
      ElfW(auxv_t) *auxv = (ElfW(auxv_t) *) interface->GetAuxiliaryVector ();
      dl_parse_auxv_t auxv_values;

      _dl_parse_auxv (auxv, auxv_values);

      /* And the vector itself, not only what was read out of it.
	 getauxval answers AT_HWCAP and AT_HWCAP2 from the values above and everything else by
	 walking this, so a null pointer here is a null dereference for any other request. rtld sets
	 it in _dl_sysdep_start, which is the startup this shim replaces -- outside a shared build
	 the only two places that set it are dl-support.c and rtld_static_init.c, both static.
	 AT_RANDOM is the request that finds it: it is where a program gets the kernel's sixteen
	 random bytes, and Qt seeds its hash tables from exactly that before main runs.  */
      GLRO (dl_auxv) = auxv;
    }

  interface->InitializeLibc = bundle_initialize_libc;

  __bundle_fill_tls_interface (interface);
}
