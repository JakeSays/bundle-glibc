/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* What libBfsRuntime needs from this libc, and the bringing-up that installs it.
 *
 * Everything here is a thing every libc does and each one spells differently -- raw syscalls, one
 * lock, errno, the page size. The shared library asks for them through a table so that it compiles
 * once rather than once per libc; this is glibc's side of it.
 *
 * The syscalls throughout rather than the public entry points, and both reasons are load bearing.
 *
 * The public pread64 and mmap are interposable, and this reader is compiled into libc: going through
 * them would let an LD_PRELOAD in the payload interpose the reads libc makes of the app it is running
 * out of. glibc keeps interposition away from its own internal use the same way.
 *
 * And every libc read entry point now consults the descriptor table -- including the internal
 * __pread64_nocancel this used to call. The reader is what the table is built on: reads run with the
 * table's lock held, and reaching a routed entry point from underneath it re-enters the table and
 * deadlocks on a lock the same thread already holds. That is not hypothetical; it hung every app the
 * moment pread64_nocancel was routed.
 *
 * The descriptor is the app's own, opened by the loader. It is never in the table, so there is
 * nothing the routing could usefully do even if it could safely run.  */

#include <bundlefs/BfsRuntime.h>

#include <errno.h>
#include <fcntl.h>
#include <ldsodefs.h>
#include <libc-lock.h>
#include <not-cancel.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sysdep.h>

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

/* Sealed the moment it exists, which is why creating and sealing are one operation rather than two.
   __fcntl64_nocancel rather than the public one, so this is not something a payload can interpose
   between the descriptor being made and being closed to writing.  */
static int
bfs_create_sealed_token (const char *name)
{
  int fd = INLINE_SYSCALL_CALL (memfd_create, name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0)
    return -1;

  /* Growing is sealed too, or the file could be extended and the new bytes written.  */
  if (__fcntl64_nocancel (fd, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK) != 0)
    {
      INLINE_SYSCALL_CALL (close, fd);
      return -1;
    }

  return fd;
}

static int
bfs_create_memory (const char *name, uint64_t length)
{
  int fd = INLINE_SYSCALL_CALL (memfd_create, name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0)
    return -1;

  if (INLINE_SYSCALL_CALL (ftruncate, fd, (off64_t) length) != 0)
    {
      INLINE_SYSCALL_CALL (close, fd);
      return -1;
    }

  return fd;
}

static int64_t
bfs_write_at (int descriptor, const void *buffer, uint64_t length, uint64_t offset)
{
  return INLINE_SYSCALL_CALL (pwrite64, descriptor, buffer, (size_t) length,
			      SYSCALL_LL64_PRW ((off64_t) offset));
}

static int
bfs_seal (int descriptor)
{
  return __fcntl64_nocancel (descriptor, F_ADD_SEALS,
			     F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK);
}

static void *
bfs_map_memory (void *address, uint64_t length, int protection, int flags, int descriptor,
		uint64_t offset)
{
  return (void *) INLINE_SYSCALL_CALL (mmap, address, (size_t) length, protection, flags,
				       descriptor, offset);
}

/* Called with the table's lock held, so the raw syscall rather than __close_nocancel -- that would
   reach BfsForget, which takes the same lock. Nothing this closes is in the table.  */
static void
bfs_close (int descriptor)
{
  INLINE_SYSCALL_CALL (close, descriptor);
}

static void
bfs_set_error (int error)
{
  __set_errno (error);
}

static uint64_t
bfs_page_size (void)
{
  return (uint64_t) __getpagesize ();
}

__libc_lock_define_initialized (static, bfs_lock);

static void
bfs_take_lock (void)
{
  __libc_lock_lock (bfs_lock);
}

static void
bfs_release_lock (void)
{
  __libc_lock_unlock (bfs_lock);
}

/* Handed on to the linker, which has the switch and the only place in the process that is not the
   payload's own output. Silent when there is no linker -- glibc outside an artifact reads no images,
   and a message with nowhere to go is not written where it would be mistaken for the payload's.  */
static void
bfs_report (const char *message)
{
  const struct RuntimeInterface *runtime = GL (dl_bundle_runtime);

  if (runtime == NULL || runtime->Report == NULL)
    return;

  runtime->Report (message);
}

static const BfsPlatformInterface bfs_platform =
  {
    .Io =
      {
	.ReadAt = bfs_read_at,
	.Map = bfs_map,
	.Unmap = bfs_unmap,
      },
    .CreateSealedToken = bfs_create_sealed_token,
    .CreateMemory = bfs_create_memory,
    .WriteAt = bfs_write_at,
    .Seal = bfs_seal,
    .MapMemory = bfs_map_memory,
    .Close = bfs_close,
    .SetError = bfs_set_error,
    .PageSize = bfs_page_size,
    .Lock = bfs_take_lock,
    .Unlock = bfs_release_lock,
    .Report = bfs_report,
  };

/* Called from __libc_early_init, after malloc, which §4.1 of design/bundle-format.md argues for: the
   reader needs a real allocator, and everything that comes earlier reads no files. Not lazily, on the
   first path that happens to be asked about -- a failure then would surface at an arbitrary open
   rather than at a point anyone can name.  */
void
__bfs_early_init (void)
{
  BfsRuntimeInitialize (&bfs_platform, GL (dl_bundle_runtime));
}

/* What the image knows, in the shape a caller of stat asked for. These stay here rather than moving
   with the rest: a stat needs a device and inode meaning something in this process's world, which an
   image has no basis for inventing, and the two libcs do not spell the structure the same way.

   No device or inode, then. What a caller usually wants them for -- telling two files apart -- is
   covered by the descriptor's own identity where there is one.  */
static unsigned int
bfs_format_bits (const BfsFileInfo *info)
{
  switch (info->Kind)
    {
    case BfsEntryKindDirectory:
      return S_IFDIR;

    case BfsEntryKindSymbolicLink:
      return S_IFLNK;

    default:
      return S_IFREG;
    }
}

void
__bfs_fill_stat (const BfsFileInfo *info, struct stat64 *buffer)
{
  memset (buffer, 0, sizeof (*buffer));

  buffer->st_mode = bfs_format_bits (info) | (info->Mode & 07777);
  buffer->st_size = (off64_t) info->Size;
  buffer->st_uid = info->UserId;
  buffer->st_gid = info->GroupId;
  buffer->st_mtime = (time_t) info->ModificationTime;
  buffer->st_atime = (time_t) info->ModificationTime;
  buffer->st_ctime = (time_t) info->ModificationTime;
  buffer->st_nlink = 1;
  buffer->st_blksize = 4096;
  buffer->st_blocks = (blkcnt64_t) ((info->Size + 511) / 512);
}

/* The image itself, in the shape statfs asked for. statvfs is derived from statfs in this libc, so
   this one fill serves all four of statfs, fstatfs, statvfs and fstatvfs.

   Free space is zero rather than unknown: an image is fixed at the size it was built, so a caller
   asking what it could write there is being told the truth. ST_RDONLY is the field that matters, and
   __internal_statvfs carries it across as f_flag ^ ST_VALID -- so ST_VALID is set here too, or
   statvfs would report a read-write filesystem for a read-only one.  */
void
__bfs_fill_statfs (const BfsFileSystemInfo *info, struct statfs64 *buffer)
{
  memset (buffer, 0, sizeof (*buffer));

  buffer->f_type = 0xE0F5E1E2;
  buffer->f_bsize = info->BlockSize;
  buffer->f_frsize = info->BlockSize;
  buffer->f_blocks = info->Blocks;
  buffer->f_bfree = 0;
  buffer->f_bavail = 0;
  buffer->f_files = info->Files;
  buffer->f_ffree = 0;
  buffer->f_namelen = info->NameMaximum;

  /* The kernel's own bit for "these flags mean something", which __internal_statvfs strips back off
     with an exclusive or on its way to statvfs. Spelled here as internal_statvfs.c spells it, since
     it is the kernel's statfs interface rather than statvfs's and <sys/statvfs.h> does not name
     it.  */
#ifndef ST_VALID
# define ST_VALID 0x0020
#endif

  buffer->f_flags = ST_VALID | ST_RDONLY;
}

/* statx asks what it wants by mask and is told what it got the same way, so the device and inode are
   left out of stx_mask rather than filled with something invented. A caller that asked for them sees
   they were not filled, which is what the mask is for.  */
void
__bfs_fill_statx (const BfsFileInfo *info, struct statx *buffer)
{
  memset (buffer, 0, sizeof (*buffer));

  buffer->stx_mask = STATX_TYPE | STATX_MODE | STATX_NLINK | STATX_UID | STATX_GID | STATX_SIZE
		     | STATX_ATIME | STATX_MTIME | STATX_CTIME;
  buffer->stx_blksize = 4096;
  buffer->stx_nlink = 1;
  buffer->stx_uid = info->UserId;
  buffer->stx_gid = info->GroupId;
  buffer->stx_mode = bfs_format_bits (info) | (info->Mode & 07777);
  buffer->stx_size = info->Size;
  buffer->stx_blocks = (info->Size + 511) / 512;

  buffer->stx_atime.tv_sec = (int64_t) info->ModificationTime;
  buffer->stx_mtime.tv_sec = (int64_t) info->ModificationTime;
  buffer->stx_ctime.tv_sec = (int64_t) info->ModificationTime;
}
