/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* What the routed syscall wrappers reach the bundle filesystem through.
 *
 * The table itself is libBfsRuntime, shared with the other libc and knowing nothing about glibc. This
 * is the glibc side of it: the visibility the wrappers need, and the four stat forms, which cannot be
 * shared because filling a struct stat is the job of whoever is answering a stat call.  */

#ifndef _BUNDLEFS_DESCRIPTORS_H
#define _BUNDLEFS_DESCRIPTORS_H 1

#include <bundlefs/BfsRuntime.h>

#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

struct statx;

/* Only libc reaches these, and every call site is behind IS_IN (libc).
 *
 * That guard is not decoration. ld.so links several of the files that route through here -- lseek64,
 * fstat64, fstatat64, opendir, getdents64 -- and a reference from any of them would pull the whole
 * reader into the loader, and malloc with it, which the loader defines itself.
 *
 * Weak was tried and does not work: ld.so's build refuses to link with any undefined symbol, weak
 * included, so a weak reference fails the check rather than resolving to nothing.
 *
 * What works is the sysdep-rtld-routines idiom already beside this -- a dl- file that includes the
 * libc source so the loader compiles its own copy with MODULE_NAME=rtld, where the guard is false and
 * the reference is not emitted at all. Every file routed here that ends up in ld.so needs one.  */

/* Brings the filesystem up. Called from __libc_early_init.  */
extern void __bfs_early_init (void) attribute_hidden;

/* What the image knows, in the shape a caller of stat or statx asked for.  */
extern void __bfs_fill_stat (const BfsFileInfo *info, struct stat64 *buffer) attribute_hidden;
extern void __bfs_fill_statx (const BfsFileInfo *info, struct statx *buffer) attribute_hidden;

/* The two halves together, since every caller wants both and doing it in one place keeps the
   combination from being written out four times.  */

static inline int
__bfs_stat_path (const char *path, struct stat64 *buffer)
{
  BfsFileInfo info;

  if (!BfsDescribePath (path, &info))
    return -1;

  __bfs_fill_stat (&info, buffer);

  return 0;
}

static inline int
__bfs_fstat (int fd, struct stat64 *buffer)
{
  BfsFileInfo info;

  if (!BfsDescribe (fd, &info))
    return -1;

  __bfs_fill_stat (&info, buffer);

  return 0;
}

static inline int
__bfs_statx_path (const char *path, struct statx *buffer)
{
  BfsFileInfo info;

  if (!BfsDescribePath (path, &info))
    return -1;

  __bfs_fill_statx (&info, buffer);

  return 0;
}

static inline int
__bfs_statx_fd (int fd, struct statx *buffer)
{
  BfsFileInfo info;

  if (!BfsDescribe (fd, &info))
    return -1;

  __bfs_fill_statx (&info, buffer);

  return 0;
}

#endif
