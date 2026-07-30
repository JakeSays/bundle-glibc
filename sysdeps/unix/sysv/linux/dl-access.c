/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* access, for the loader. See dl-lseek64.c for why the loader has its own.
 *
 * It arrives because rtld calls __access directly while deciding what it may load.  */

#include <access.c>
