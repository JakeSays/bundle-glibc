/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* Bringing the bundle filesystem up inside libc, and holding it afterwards.

   Called from __libc_early_init, after malloc, which §4.1 of design/bundle-format.md argues for:
   the reader needs a real allocator, and everything that comes earlier reads no files. Not lazily,
   on the first path that happens to be asked about -- a failure then would surface at an arbitrary
   open rather than at a point anyone can name.  */

#include <bundlefs/BundleFs.h>

#include <dl-minst-bundle.h>
#include <errno.h>
#include <fcntl.h>
#include <not-cancel.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sysdep.h>

/* The path space this process runs against, or NULL when it is not running out of an artifact.  */
static BfsFileSystem *process_file_system;

/* Reading and mapping the artifact by syscall, not through any libc entry point.

   Two reasons, and the second is fatal rather than merely undesirable.

   The public pread64 and mmap are interposable, and this reader is compiled into libc: going through
   them would let an LD_PRELOAD in the payload interpose the reads libc makes of the artifact it is
   running out of. glibc keeps interposition away from its own internal use the same way.

   And every libc read entry point now consults the descriptor table -- including the internal
   __pread64_nocancel this used to call. The reader is what the table is built on: BfsFileRead runs
   with the table's lock held, and reaching a routed entry point from underneath it re-enters the
   table and deadlocks on a lock the same thread already holds. That is not hypothetical; it hung
   every artifact the moment pread64_nocancel was routed.

   The descriptor here is the artifact's own, opened by the loader. It is never in the table, so
   there is nothing the routing could usefully do even if it could safely run.  */

static int64_t
bfs_read_at (int descriptor, uint64_t offset, void *buffer, uint64_t length)
{
  long got = INLINE_SYSCALL_CALL (pread64, descriptor, buffer, length,
				  SYSCALL_LL64_PRW ((off64_t) offset));

  return got < 0 ? -errno : got;
}

static int
bfs_map (int descriptor, uint64_t offset, uint64_t length, const void **data)
{
  long mapped = INLINE_SYSCALL_CALL (mmap, NULL, length, PROT_READ, MAP_PRIVATE, descriptor,
				     offset);

  if (mapped < 0 && mapped > -4096)
    return (int) mapped;

  *data = (const void *) mapped;

  return 0;
}

static void
bfs_unmap (const void *data, uint64_t length)
{
  INLINE_SYSCALL_CALL (munmap, (void *) data, length);
}

static const BfsImageIo bfs_io =
  {
    .ReadAt = bfs_read_at,
    .Map = bfs_map,
    .Unmap = bfs_unmap,
  };

void
__bfs_early_init (void)
{
  const struct minst_bundle_view *view = _dl_minst_bundle_view ();

  /* No artifact around us, so no descriptor and no images. Every path falls through to the syscall
     and the process behaves as though none of this existed. Silent, because nothing is wrong.  */
  if (view == NULL)
    return;

  if (BfsFileSystemCreate (&process_file_system) != 0)
    return;

  for (uint32_t index = 0; index < view->image_count; ++index)
    {
      const struct minst_bundle_image *description = &view->images[index];

      BfsImageOptions options = { 0 };
      options.Mappable = true;
      options.Io = &bfs_io;

      BfsImage *image = NULL;
      if (BfsImageOpen (view->descriptor, description->offset, description->length, &options,
			&image) != 0)
	continue;

      BfsFileSystemMount (process_file_system, description->mount_point, image,
			  description->image_path);
    }
}

/* The path space libc brought up, for a payload that would rather map its own data than have it
   copied through a descriptor. Overrides the weak definition bundlefs carries for a program linking
   the library on its own, which has nothing to hand back.  */
BfsFileSystem *
BfsProcessFileSystem (void)
{
  return process_file_system;
}
