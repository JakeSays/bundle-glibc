/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* The non-cancellable close, for the loader.
 *
 * The same source compiled with MODULE_NAME=rtld, for the reason dl-lseek64.c gives in full. Without
 * it the loader takes close_nocancel.os out of libc_pic.a, and that object now reaches the descriptor
 * table -- which reaches malloc, which the loader defines itself.
 *
 * The loader closes what it opens and none of it is ours: it maps from extents recorded in the view
 * note, and never opens a path through the bundle filesystem.  */

/* No hidden alias here, unlike the dl- files beside it: not-cancel.h applies hidden_proto for rtld as
 * well as for libc, so the declaration already carries the __GI_ asm name and the definition below
 * takes it on its own.  */

#include <close_nocancel.c>
