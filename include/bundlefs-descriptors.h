/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* Descriptors on paths the artifact carries, and the calls that act on them.

   A bundled file is read through bundlefs rather than copied out of it. That costs libc a table --
   every read, lseek, fstat and close has to ask whether this descriptor is one of ours -- and it buys
   the thing that matters: a file in an image is read where it lies, so opening a large member costs
   nothing and holds nothing.

   The alternative was an anonymous file holding a copy, which needs no table because the kernel owns
   everything afterwards. It also materialises every byte of every member anything opens, which for a
   locale archive or an asset tree is the whole point of not doing it.

   The number is a real descriptor the kernel gave out, so it cannot collide with one and it behaves
   as a descriptor should when it is duplicated, inherited or closed. What it does not hold is content.

   Every one of these returns a sentinel meaning "not ours" so the caller falls through to the syscall,
   which is the common case: an artifact carries a handful of paths and a process opens hundreds.  */

#ifndef _BUNDLEFS_DESCRIPTORS_H
#define _BUNDLEFS_DESCRIPTORS_H 1

#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>

struct statx;

/* Only libc reaches these, and every call site is behind IS_IN (libc).

   That guard is not decoration. ld.so links several of the files that route through here -- lseek64,
   fstat64, fstatat64, opendir, getdents64 -- and a reference from any of them would pull the whole
   reader into the loader, and malloc with it, which the loader defines itself.

   Weak was tried and does not work: ld.so's build refuses to link with any undefined symbol, weak
   included, so a weak reference fails the check rather than resolving to nothing.

   What works is the sysdep-rtld-routines idiom already beside this -- a dl- file that includes the
   libc source so the loader compiles its own copy with MODULE_NAME=rtld, where the guard is false and
   the reference is not emitted at all. Every file routed here that ends up in ld.so needs one.  */

/* A descriptor on what the artifact carries at PATH, or -1 when it carries nothing there or the flags
   ask for something an image cannot answer.  */
extern int __bfs_open_path (const char *path, int flags) attribute_hidden;

/* Whether this descriptor is one of ours. Every routed call asks first, and the answer is no almost
   always.

   It is also the whole of the write guard. An image is read-only and __bfs_open_path refuses every
   flag that writes, so a descriptor being ours is enough to know it was opened for reading -- write,
   pwrite, writev and ftruncate need no more than this to refuse with EBADF.

   They have to refuse. The number underneath is an anonymous file, which the kernel considers
   writable whatever the caller asked for, so a write would quietly succeed and land somewhere no
   later read would ever look.  */
extern bool __bfs_owns (int fd) attribute_hidden;

extern ssize_t __bfs_read (int fd, void *buffer, size_t length) attribute_hidden;
extern ssize_t __bfs_pread (int fd, void *buffer, size_t length, off_t offset) attribute_hidden;

/* The vector forms, filling the buffers in order under a single lock -- so the position cannot move
   between them, which it could if this were a loop of __bfs_read.  */
extern ssize_t __bfs_readv (int fd, const struct iovec *vector, int count) attribute_hidden;
extern ssize_t __bfs_preadv (int fd, const struct iovec *vector, int count, off_t offset)
  attribute_hidden;
extern off_t __bfs_lseek (int fd, off_t offset, int whence) attribute_hidden;
extern int __bfs_fstat (int fd, struct stat64 *buffer) attribute_hidden;

/* What the caller opened with, or -1 when the descriptor is not ours. The number underneath is an
   anonymous file the kernel made read-write, so asking it would report a mode the caller never
   asked for -- and anything that checks before writing would believe it.  */
extern int __bfs_flags (int fd) attribute_hidden;

/* Forgets the descriptor, closing the file behind it when this was the last number naming it. The
   caller still closes the number itself: the kernel gave it out and the kernel takes it back.  */
extern void __bfs_forget (int fd) attribute_hidden;

/* The same for every number in [FIRST, LAST], which is what close_range closes in one syscall --
   posix_spawn does this on every launch. Without it the table keeps records for numbers the kernel
   has taken back, and the next memfd_create is handed one of them with a dead file behind it.  */
extern void __bfs_forget_range (unsigned int first, unsigned int last) attribute_hidden;

/* Makes TO name whatever FROM names, which is what dup and its relatives do. They share the file and
   its read position, because the kernel shares them.

   Anything TO named before is let go first, exactly as the kernel closes the number it is about to
   reuse. Harmless when FROM is not ours: TO simply stops being ours too, which is what dup2 onto one
   of our numbers has to do.

   False only when the table could not grow, which leaves TO a real descriptor on an empty file --
   the caller closes it and reports ENOMEM rather than hand back something that reads as nothing.  */
extern bool __bfs_adopt (int from, int to) attribute_hidden;

/* Works out the path openat and its relatives mean by (FD, PATH), writing it to BUFFER.

   An absolute path stands alone and AT_FDCWD leaves the path as it is; anything else is taken
   relative to the directory FD names, which the table knows because it recorded the path each
   descriptor was opened by. The kernel cannot answer this for us -- the number it holds is an
   anonymous file with no name in any tree.

   False when the path cannot be ours: FD is not one of our directories, or the result would not
   fit. The caller then goes to the kernel, which is right in both cases.  */
extern bool __bfs_resolve_at (int fd, const char *path, char *buffer, size_t length)
  attribute_hidden;

/* The statx forms of the same, which report what they answered in stx_mask rather than filling every
   field. Device and inode are left unanswered rather than invented.  */
extern int __bfs_statx_path (const char *path, struct statx *buffer) attribute_hidden;
extern int __bfs_statx_fd (int fd, struct statx *buffer) attribute_hidden;

/* Bytes out of the image for a caller that has to hand them to another descriptor -- sendfile, whose
   kernel form cannot help here because ours holds an empty anonymous file. The write is the caller's,
   done outside the table's lock.  */
extern ssize_t __bfs_sendfile_read (int fd, void *buffer, size_t length, off_t *offset)
  attribute_hidden;

/* Answering about a path rather than a descriptor. Nothing is opened and nothing is held: the image
   already knows what it has, so these cost a lookup and no more.  */
extern int __bfs_stat_path (const char *path, struct stat64 *buffer) attribute_hidden;
extern bool __bfs_carries (const char *path) attribute_hidden;
extern ssize_t __bfs_readlink_path (const char *path, char *buffer, size_t length) attribute_hidden;

/* A descriptor on a carried directory, or -1 when the artifact carries none there. A number and a
   table entry, the same as a file: nothing is listed until something asks.  */
extern int __bfs_opendir_fd (const char *path) attribute_hidden;

/* Entries written straight into the caller's buffer as they are read, in the layout getdents64
   returns. Nothing is duplicated on the way: BfsDirectoryEntry hands back a pointer into the reader's
   own storage, which is what it is shaped for.  */
extern ssize_t __bfs_getdents64 (int fd, void *buffer, size_t length) attribute_hidden;

/* A mapping of a carried file, or MAP_FAILED with errno set.

   The program's own mapping of the artifact itself, at the offset the content lies at, so the pages
   come from the page cache and two processes running the same artifact share them. That is the whole
   reason files are stored contiguously and the image is placed on a page boundary.

   A file that is not a run of whole blocks -- compressed, or with its tail packed into its inode --
   has no offset to name, and is materialised into the descriptor and mapped from there. That copy is
   the exception the arrangement exists to avoid, and it happens only where there is no alternative.  */
extern void *__bfs_mmap (void *address, size_t length, int protection, int flags, int fd,
			 off64_t offset) attribute_hidden;

#endif
