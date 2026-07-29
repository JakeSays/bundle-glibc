/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* What erofs-utils expects autoconf to have written, for the one build where autoconf cannot run.

   Force-included rather than reached as <config.h>, which is what erofs asks for under
   HAVE_CONFIG_H: glibc has a config.h of its own and the two would collide. HAVE_CONFIG_H stays
   undefined here and this arrives through -include instead.

   Nothing is probed. Every question below is a question about the platform, and this is the one
   build where the platform is known in advance -- it is glibc, on Linux, being compiled by the
   compiler that is about to build the rest of libc. bundlefs generates the same answers with cmake
   for builds that could be anywhere.  */

#ifndef _BUNDLEFS_EROFS_CONFIG_H
#define _BUNDLEFS_EROFS_CONFIG_H 1

/* Headers. */
#define HAVE_ENDIAN_H 1
#define HAVE_UNISTD_H 1
#define HAVE_PTHREAD_H 1
#define HAVE_LINUX_TYPES_H 1
#define HAVE_LINUX_FS_H 1
#define HAVE_LINUX_FALLOC_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_STATFS_H 1
#define HAVE_SYS_SYSMACROS_H 1

/* Functions. */
#define HAVE_PREAD64 1
#define HAVE_PWRITE64 1
#define HAVE_PWRITEV 1
#define HAVE_FALLOCATE 1
#define HAVE_FSTATFS 1
#define HAVE_COPY_FILE_RANGE 1
#define HAVE_SYSCONF 1
#define HAVE_MEMRCHR 1

/* Struct members that vary by libc, and do not vary here. */
#define HAVE_STRUCT_STAT_ST_ATIM 1

/* Reported by erofs in its own configuration and nowhere else. It has no consumer in a reader, so
   it exists to satisfy the reference rather than to be read.  */
#define PACKAGE_VERSION "1.9.2"

/* A compile-time ceiling on the block size of any image this build will open, since it bounds stack
   buffers in the reader. Stated rather than taken from the page size of whatever machine compiled
   libc, which has nothing to do with it.  */
#define EROFS_MAX_BLOCK_SIZE 4096

/* Deliberately absent, and each absence chooses a code path: no compression on either side, since
   nothing decompresses inside libc yet; no curl, selinux, libxml, json-c, openssl, liburing, qpl or
   libuuid, none of which libc is going to grow a dependency on.  */

#endif
