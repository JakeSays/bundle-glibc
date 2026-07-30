/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* mmap, for the loader.
 *
 * The same source compiled with MODULE_NAME=rtld, for the reason dl-lseek64.c gives in full. Without
 * it the loader takes mmap64.os out of libc_pic.a, and that object now reaches the descriptor table.
 *
 * The loader has no use for the routing anyway. It maps objects from the extents recorded in the view
 * note, holding the artifact's own descriptor, so it is already doing by hand what the routed mmap
 * does for everybody else -- and it has to, since bundlefs cannot come up until libc is mapped.  */

#include <mmap64.c>

/* The hidden aliases as well, which libc_hidden_def does not emit outside libc. Everything in the
 * loader that maps calls __mmap by its hidden name.  */
strong_alias (__mmap64, __GI___mmap64)

#ifdef __OFF_T_MATCHES_OFF64_T
strong_alias (__mmap64, __GI___mmap)
#endif
