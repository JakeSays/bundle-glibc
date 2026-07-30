/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* The non-cancellable, status-free close, for the loader.
 *
 * The same source compiled with MODULE_NAME=rtld, for the reason dl-lseek64.c gives in full. The
 * loader closes descriptors with this, so it takes the object regardless, and that object now
 * reaches the descriptor table.
 *
 * No hidden alias here, as in dl-close_nocancel.c: not-cancel.h applies hidden_proto for rtld as
 * well as for libc, so the definition already carries the __GI_ name the loader calls it by.  */

#include <close_nocancel_nostatus.c>
