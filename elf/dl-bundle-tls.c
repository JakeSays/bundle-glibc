/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* Where thread-local storage goes, answered for the linker.
 *
 * The layout itself is glibc's and is not reimplemented here. _dl_determine_tlsoffset does it, and
 * everything below exists to give that function what it reads and to report what it wrote.
 *
 * That shapes the whole file, because _dl_determine_tlsoffset takes no arguments: it walks the base
 * namespace's chain of struct link_map, reads l_tls_blocksize, l_tls_firstbyte_offset and
 * l_tls_align off each one, and writes l_tls_offset back. There is no way to place a single module.
 * So placing records a map, and the offsets exist only once the layout is frozen -- which is what
 * the runtime interface separates PlaceModule from GetModulePlacement for.
 *
 * Phase one runs before anything has been relocated, which rules out allocating. dl_main callocs
 * both the link maps and the slotinfo list; neither can be done here, so both are static arrays. The
 * same answer csu/libc-tls.c already gives for a program with no loader at all.  */

#include <bundle/RuntimeInterface.h>

#include <dl-tls.h>
#include <errno.h>
#include <ldsodefs.h>
#include <rtld-malloc.h>
#include <stdbool.h>
#include <string.h>

/* Out of alphabetical order deliberately, and it has to be.  It declares _dl_try_allocate_static_tls
   taking a struct link_map * and a bool, and includes neither what defines the first nor what
   declares the second, so it compiles only after ldsodefs.h and stdbool.h above.  */
#include <dl-static-tls.h>

/* After ldsodefs.h rather than in order with the rest: it calls __tls_init_tp, which ldsodefs.h is
   what declares. rtld.c includes it in the same position for the same reason.  */
#include <dl-call_tls_init_tp.h>

/* How many modules may carry thread-local storage.
 *
 * A fixed array because there is no allocator during phase one. Generous rather than tight: a
 * struct link_map is large, but the alternative is an app that fails to start for having one library
 * too many, and this is bss that is never touched past the modules in use.  */
#define BundleModuleCapacity 64

static struct link_map _maps[BundleModuleCapacity];
static uint32_t _mapCount;

/* Room for every module plus what glibc's own list leaves spare, so that _dl_add_to_slotinfo never
   reaches its malloc path. Laid out as one object so the array follows the header exactly as the
   allocated form does.  */
static struct
{
  struct dtv_slotinfo_list list;
  struct dtv_slotinfo slots[BundleModuleCapacity + TLS_SLOTINFO_SURPLUS];
} _slotinfo;

static int _layoutFinished;

/* The chain _dl_determine_tlsoffset walks, and the only thing that walks it.  */
static struct link_map *_head;

/* The executable's map, which the psABI fixes relative to the thread control block and which glibc
   expects to be the first thing in the chain.  */
static struct link_map *_executable;

/* Fills the thread-local fields of a map from what the linker read out of PT_TLS.
 *
 * l_tls_firstbyte_offset is p_vaddr modulo p_align. The static linker laid the module's data out
 * assuming the block's first byte satisfies p_vaddr mod p_align == &block mod p_align, so a segment
 * aligned to a nonzero remainder needs that remainder carried across or every offset inside it is
 * wrong by the difference.  */
static struct link_map *
bundle_record (const BundleTlsSegment *segment)
{
  if (segment == NULL || segment->Size == 0 || _mapCount >= BundleModuleCapacity
      || _layoutFinished)
    return NULL;

  struct link_map *map = &_maps[_mapCount++];

  map->l_tls_blocksize = segment->Size;
  map->l_tls_initimage = (void *) segment->Initializer;
  map->l_tls_initimage_size = segment->InitializedSize;
  map->l_tls_align = segment->Alignment;
  map->l_tls_firstbyte_offset = segment->AlignmentSkew;

  /* Not in the static block yet, which is what glibc reads this as. _dl_determine_tlsoffset writes
     the real value.  */
  map->l_tls_offset = NO_TLS_OFFSET;

  /* Reads and advances GL(dl_tls_max_dtv_idx). It does not touch the slotinfo list, which is why it
     can run here and the list can wait for the freeze.  */
  _dl_assign_tls_modid (map);

  /* Chained privately, in the order placed, because that is the order _dl_determine_tlsoffset lays
     them out in and the executable has to come first.
     Not onto GL(dl_ns)[LM_ID_BASE]._ns_loaded, which is what it looks like it should be. That list
     is glibc's record of what is loaded, and these are not that -- they carry thread-local fields
     and nothing else, so anything walking the list for another reason finds a map whose l_info is
     entirely zero. Nothing in libc walks it any more, and this is the half of that change that keeps
     it true.  */
  if (_head == NULL)
    _head = map;
  else
    {
      struct link_map *last = _head;

      while (last->l_next != NULL)
	last = last->l_next;

      last->l_next = map;
      map->l_prev = last;
    }

  return map;
}

static uint32_t
bundle_place_executable (const BundleTlsSegment *segment)
{
  /* Nothing to reserve for the thread control block. glibc's is the struct pthread at the thread
     pointer, and _dl_determine_tlsoffset accounts for it when it sizes the block.  */
  if (segment == NULL || segment->Size == 0)
    return 0;

  _executable = bundle_record (segment);

  return _executable == NULL ? 0 : (uint32_t) _executable->l_tls_modid;
}

static uint32_t
bundle_place_module (const BundleTlsSegment *segment)
{
  struct link_map *map = bundle_record (segment);

  return map == NULL ? 0 : (uint32_t) map->l_tls_modid;
}

static void
bundle_finish_layout (uint64_t surplusBytes)
{
  if (_layoutFinished)
    return;

  /* What rtld's own startup would have done, done here because this is the last moment it can be.
     Everything these two touch lives in .data.rel.ro -- the four allocator pointers carry
     attribute_relro, and so do the rtld mutex stubs -- and the linker turns that read-only when it
     finishes linking, which is between this phase and the next.
     Neither is needed yet. __rtld_malloc_init_stubs is for the allocation phase two makes, and
     __tls_pre_init_tp puts the three thread-stack lists into the empty state __tls_init_tp will add
     the initial thread to. Both would fault if left until then.  */
  __rtld_malloc_init_stubs ();
  __tls_pre_init_tp ();

  /* Static rather than callocated, which is what dl_main does here. There is no allocator during
     phase one, and this is the same answer csu/libc-tls.c gives.  */
  _slotinfo.list.len = BundleModuleCapacity + TLS_SLOTINFO_SURPLUS;
  _slotinfo.list.next = NULL;
  memset (_slotinfo.slots, 0, sizeof (_slotinfo.slots));

  GL (dl_tls_dtv_slotinfo_list) = &_slotinfo.list;

  for (uint32_t i = 0; i < _mapCount; ++i)
    _dl_add_to_slotinfo (&_maps[i], true);

  GL (dl_tls_static_nelem) = GL (dl_tls_max_dtv_idx);

  /* What a library arriving by dlopen may need if it was compiled for the initial-exec model. The
     linker takes it from the view note, where the bundler put it having seen the whole closure --
     which is a better number than the one glibc computes for itself from tunables it will never be
     told.  */
  GLRO (dl_tls_static_surplus) = surplusBytes;

  _dl_determine_tlsoffset (_head);

  _layoutFinished = 1;
}

static BundleTlsPlacement
bundle_get_placement (uint32_t moduleId)
{
  BundleTlsPlacement placement;

  placement.ModuleId = 0;
  placement.IsStatic = 0;
  placement.ThreadPointerOffset = 0;

  if (moduleId == 0 || !_layoutFinished)
    return placement;

  for (uint32_t i = 0; i < _mapCount; ++i)
    {
      if ((uint32_t) _maps[i].l_tls_modid != moduleId)
	continue;

      if (_maps[i].l_tls_offset == NO_TLS_OFFSET
	  || _maps[i].l_tls_offset == FORCED_DYNAMIC_TLS_OFFSET)
	return placement;

      placement.ModuleId = moduleId;
      placement.IsStatic = 1;

      /* Where TLS_TCB_AT_TP puts it: the control block sits at the thread pointer and the modules
	 below it, so l_tls_offset is a distance downward and the signed form the linker wants is its
	 negation.  */
      placement.ThreadPointerOffset = -(int64_t) _maps[i].l_tls_offset;

      return placement;
    }

  return placement;
}

/* The C implementation of __tls_get_addr, which on x86-64 is renamed so that the assembly wrapper in
   tls_get_addr-compat.S can carry the public name -- see sysdeps/x86_64/dl-tls.c. That wrapper exists
   to realign the stack for callers emitted by a compiler with GCC PR58066; an ordinary C call from
   here already satisfies the ABI, so this goes straight to the implementation.  */
extern void *___tls_get_addr (tls_index *ti) attribute_hidden;

/* The slotinfo entry for a module id.
 *
 * The list is a chain of blocks rather than one array, because it grows as modules arrive, so an
 * index has to be walked to rather than subscripted.  */
static struct dtv_slotinfo *
bundle_slotinfo_entry (size_t modid)
{
  struct dtv_slotinfo_list *list = GL (dl_tls_dtv_slotinfo_list);
  size_t base = 0;

  while (list != NULL)
    {
      if (modid < base + list->len)
	return &list->slotinfo[modid - base];

      base += list->len;
      list = list->next;
    }

  return NULL;
}

/* A module arrived by dlopen, after the static block was sized and frozen.
 *
 * Its storage does not go in that block. The block's size is fixed at the moment the first thread is
 * built and every thread since has one of that size; there is no way to widen the ones that already
 * exist. So the module gets a dynamic thread vector slot instead, and its storage is allocated per
 * thread the first time that thread touches it.
 *
 * Left at NO_TLS_OFFSET rather than FORCED_DYNAMIC_TLS_OFFSET, which is what dl_open_worker does and
 * for the same reason: the two are not synonyms. NO_TLS_OFFSET means undecided, and keeps
 * bundle_promote_to_static available if an initial-exec relocation turns up later. FORCED_DYNAMIC
 * means decided, and _dl_try_allocate_static_tls refuses it outright. glibc sets it in __tls_get_addr
 * at the first dynamic access, which is the point after which promoting would move storage a thread
 * is already holding a pointer to.
 *
 * Phase two, so there is an allocator and this can do what dl_open does.  */
static BundleTlsPlacement
bundle_add_dynamic_module (const BundleTlsSegment *segment)
{
  BundleTlsPlacement placement;

  placement.ModuleId = 0;
  placement.IsStatic = 0;
  placement.ThreadPointerOffset = 0;

  if (segment == NULL || segment->Size == 0 || !_layoutFinished)
    return placement;

  struct link_map *map = calloc (1, sizeof (struct link_map));

  if (map == NULL)
    return placement;

  map->l_tls_blocksize = segment->Size;
  map->l_tls_initimage = (void *) segment->Initializer;
  map->l_tls_initimage_size = segment->InitializedSize;
  map->l_tls_align = segment->Alignment;
  map->l_tls_firstbyte_offset = segment->AlignmentSkew;
  map->l_tls_offset = NO_TLS_OFFSET;
  map->l_real = map;

  _dl_assign_tls_modid (map);

  /* Twice, and in this order, which is what dl_open does. The first pass only makes room in the
     slotinfo list and is the half that can fail; the second records the module and cannot. Splitting
     them is what keeps a failed allocation from leaving a half-registered module behind.  */
  _dl_add_to_slotinfo (map, false);
  _dl_add_to_slotinfo (map, true);

  /* Every thread's dynamic thread vector is now a generation behind, which is how each one learns to
     pick this module up on its next access rather than being updated here.  */
  atomic_store_release (&GL (dl_tls_generation), GL (dl_tls_generation) + 1);

  placement.ModuleId = (uint32_t) map->l_tls_modid;

  return placement;
}

/* Where this thread's copy of a module's storage is.
 *
 * glibc's __tls_get_addr, under the name the linker calls it by. It allocates on first touch and
 * brings the calling thread's vector up to the current generation, so a module that arrived after a
 * thread started is reached here without that thread having been involved in the load.  */
static void *
bundle_thread_local_address (uint32_t moduleId, uint64_t offset)
{
  tls_index ti;

  ti.ti_module = moduleId;
  ti.ti_offset = offset;

  return ___tls_get_addr (&ti);
}

/* A module that arrived by dlopen is referenced with the initial-exec model.
 *
 * That access is a fixed distance from the thread pointer, decided when the referring object was
 * compiled, so the per-thread storage bundle_add_dynamic_module gave it cannot serve. The only
 * storage in the process at a fixed offset that is not already spoken for is the slack
 * bundle_finish_layout reserved, and this is what moves the module into it.
 *
 * _dl_try_allocate_static_tls does the whole of it: it checks the module fits in what is left,
 * assigns l_tls_offset out of the reserve, and calls _dl_init_static_tls to copy the initializer into
 * every live thread's block -- the storage is at the same offset in each of them, which is exactly
 * why this works and why a list of separately allocated chunks could not.
 *
 * Under dl_load_tls_lock, which is what the same call expects in dl-reloc.c, and what keeps a thread
 * in __tls_get_addr from settling on FORCED_DYNAMIC while this is deciding otherwise.  */
static BundleTlsPlacement
bundle_promote_to_static (uint32_t moduleId)
{
  BundleTlsPlacement placement;

  placement.ModuleId = 0;
  placement.IsStatic = 0;
  placement.ThreadPointerOffset = 0;

  if (moduleId == 0 || !_layoutFinished)
    return placement;

  struct dtv_slotinfo *slot = bundle_slotinfo_entry (moduleId);

  if (slot == NULL || slot->map == NULL)
    return placement;

  struct link_map *map = slot->map;

  __rtld_lock_lock_recursive (GL (dl_load_tls_lock));

  const int failed = _dl_try_allocate_static_tls (map, false);

  __rtld_lock_unlock_recursive (GL (dl_load_tls_lock));

  if (failed)
    return placement;

  placement.ModuleId = moduleId;
  placement.IsStatic = 1;

  /* The same negation bundle_get_placement does, and for the same reason: TLS_TCB_AT_TP puts the
     modules below the thread pointer, so l_tls_offset is a distance downward.  */
  placement.ThreadPointerOffset = -(int64_t) map->l_tls_offset;

  return placement;
}

/* A module from bundle_add_dynamic_module is going away.
 *
 * The storage already handed to live threads is not reclaimed here. Their dynamic thread vectors
 * still point at it, and there is no moment at which none of them do -- glibc frees those from
 * _dl_deallocate_tls as each thread exits. What this does is retire the id, so that threads created
 * afterwards do not initialize a module that is no longer there.  */
static void
bundle_remove_dynamic_module (uint32_t moduleId)
{
  if (moduleId == 0)
    return;

  struct dtv_slotinfo *slot = bundle_slotinfo_entry (moduleId);

  if (slot == NULL || slot->map == NULL)
    return;

  free (slot->map);
  slot->map = NULL;

  /* Give the id back, but only when nothing above it is still in use. Ids are handed out by
     advancing dl_tls_max_dtv_idx, so lowering it while a higher module is live would hand the same
     id to two modules.  */
  size_t highest = GL (dl_tls_static_nelem);

  for (size_t at = GL (dl_tls_max_dtv_idx); at > GL (dl_tls_static_nelem); --at)
    {
      struct dtv_slotinfo *entry = bundle_slotinfo_entry (at);

      if (entry != NULL && entry->map != NULL)
	{
	  highest = at;

	  break;
	}
    }

  atomic_store_relaxed (&GL (dl_tls_max_dtv_idx), highest);
  atomic_store_release (&GL (dl_tls_generation), GL (dl_tls_generation) + 1);
}

/* Builds the initial thread's block and installs the thread pointer.
 *
 * Phase two, so ordinary relocated code, and it follows what dl_main does in the same order.
 *
 * The allocator it uses was stood up back in phase one, and had to be. __rtld_calloc and its three
 * siblings carry attribute_relro, so they sit in .data.rel.ro -- which the linker write-protects at
 * the end of linking, between the two phases. Setting them here is a write to a read-only page.  */
static int32_t
bundle_initialize_main_thread (void)
{
  void *tcbp = _dl_allocate_tls_storage ();

  if (tcbp == NULL)
    return ENOMEM;

  /* So that __tls_get_addr knows not to hand this one to realloc.  */
  GL (dl_initial_dtv) = GET_DTV (tcbp);

  /* Installs %fs, and then everything about a thread that is not its storage: the stack list, the
     tid, the robust mutex list, rseq. Fatal inside rather than returning, which is glibc's own
     judgment about a thread that cannot be brought up.  */
  call_tls_init_tp (tcbp);

  /* _dl_add_to_slotinfo recorded each module at dl_tls_generation + 1, and _dl_allocate_tls_init
     asserts the generation it finds is not ahead of the current one. dl_main bumps it here for the
     same reason.  */
  if (GL (dl_tls_max_dtv_idx) > 0)
    ++GL (dl_tls_generation);

  /* The images are copied last, and that is the point of doing it here rather than at the freeze:
     every module has been relocated, so what gets copied is the relocated initializer rather than
     the one that was read off disk.  */
  _dl_allocate_tls_init (tcbp, true);

  return 0;
}

void
__bundle_fill_tls_interface (RuntimeInterface *interface)
{
  interface->PlaceExecutable = bundle_place_executable;
  interface->PlaceModule = bundle_place_module;
  interface->FinishLayout = bundle_finish_layout;
  interface->GetModulePlacement = bundle_get_placement;
  interface->InitializeMainThread = bundle_initialize_main_thread;
  interface->AddDynamicModule = bundle_add_dynamic_module;
  interface->PromoteToStatic = bundle_promote_to_static;
  interface->ThreadLocalAddress = bundle_thread_local_address;
  interface->RemoveDynamicModule = bundle_remove_dynamic_module;
}
