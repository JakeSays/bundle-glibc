/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* lseek64, for the loader.
 *
 * The same source compiled with MODULE_NAME=rtld, which is what dl-getcwd.c and dl-opendir.c beside it
 * already do. That makes IS_IN (libc) false, so the bundle filesystem consult inside is not emitted at
 * all -- which is the only way to keep it out of ld.so, since ld.so's build refuses to link with any
 * undefined symbol and a weak reference would fail that check rather than resolve to nothing.
 *
 * The loader could not use one anyway: bundlefs comes up inside __libc_early_init, which cannot run
 * until libc is mapped, which is the loader's own job.  */

#include <lseek64.c>

/* The hidden alias as well, which libc_hidden_def does not emit outside libc. rewinddir arrives in
 * the loader because rtld calls __rewinddir, and it calls its neighbours by their hidden names --
 * without this, that reference reaches for libc's lseek64 and brings back the object this file exists
 * to keep out.  */
#if defined __OFF_T_MATCHES_OFF64_T
strong_alias (__lseek64, __GI___lseek)
#endif
