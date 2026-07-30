/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* The non-cancellable pread, for the loader.
 *
 * The same source compiled with MODULE_NAME=rtld, for the reason dl-lseek64.c gives in full. The
 * loader reads ELF headers with this, so it takes the object whether or not it is wanted, and that
 * object now reaches the descriptor table.
 *
 * No hidden alias here, as in dl-close_nocancel.c: not-cancel.h applies hidden_proto for rtld as
 * well as for libc, so the definition already carries the __GI_ name the loader calls it by.  */

#include <pread64_nocancel.c>
