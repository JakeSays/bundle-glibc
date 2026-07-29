/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* Bringing the bundle filesystem up, called from __libc_early_init.

   Declared here rather than in bundlefs's own headers because it is not part of the reader's
   interface: nothing outside libc has any business calling it, and a program linking the reader on
   its own brings up its own path space instead.  */

#ifndef _BUNDLEFS_EARLY_INIT_H
#define _BUNDLEFS_EARLY_INIT_H 1

/* Opens the images this process's artifact carries and applies their mounts. Does nothing at all
   when there is no artifact, which is the ordinary case.  */
extern void __bfs_early_init (void) attribute_hidden;

#endif
