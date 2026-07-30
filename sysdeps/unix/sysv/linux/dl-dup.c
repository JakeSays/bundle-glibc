/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* dup, for the loader.
 *
 * The same source compiled with MODULE_NAME=rtld, for the reason dl-lseek64.c gives in full. Without
 * it the loader takes dup.os out of libc_pic.a, and that object now reaches the descriptor table.
 *
 * The loader calls this on the artifact's own descriptor, in dl-load.c, which is a number it opened
 * itself and never one of the table's. There is nothing here for the routing to do.  */

#include <dup.c>
