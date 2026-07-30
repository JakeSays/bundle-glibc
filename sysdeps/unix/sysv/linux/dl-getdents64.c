/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* getdents64, for the loader. See dl-lseek64.c for why the loader has its own.
 *
 * It arrives because readdir64 does: rtld reads directories while searching for objects, and readdir
 * calls this.  */

#include <getdents64.c>

/* The hidden alias too, which libc_hidden_def does not emit outside libc. readdir64 arrives in the
 * loader and calls its neighbour by that name; without this it reaches for libc's getdents64 and
 * brings back the object this file exists to keep out.  */
strong_alias (__getdents64, __GI___getdents64)
