/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* The non-cancellable open, for the loader.
 *
 * The same source compiled with MODULE_NAME=rtld, for the reason dl-lseek64.c gives in full. The
 * loader opens shared objects with this, so it takes the object regardless, and that object now
 * consults the bundle filesystem.
 *
 * No hidden alias here, as in dl-close_nocancel.c: not-cancel.h applies hidden_proto for rtld as
 * well as for libc, so the definition already carries the __GI_ names -- both of them, since
 * __open_nocancel is a strong alias of __open64_nocancel on this ABI.  */

#include <open64_nocancel.c>
