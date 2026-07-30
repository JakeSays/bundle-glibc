/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* The non-cancellable fcntl, for the loader.
 *
 * The same source compiled with MODULE_NAME=rtld, for the reason dl-lseek64.c gives in full. The
 * loader uses this, and the object now consults the descriptor table -- F_GETFL is answered from it,
 * and F_DUPFD has to reach it, since duplicating a number makes a second name for one open file.
 *
 * No hidden alias here, as in dl-close_nocancel.c: not-cancel.h applies hidden_proto for rtld as
 * well as for libc, so the definition already carries the __GI_ name the loader calls it by.  */

#include <fcntl_nocancel.c>
