/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* What the loader knows about the artifact, told to libc.

   The two are separate modules and only one of them opened the artifact. The loader has the
   descriptor and has already read the note; libc has malloc, and is where the bundle filesystem
   comes up. This is the whole of what crosses between them.

   A function rather than a field in _rtld_global_ro: a function is a smaller promise than a struct
   layout, and that struct is shared ground the fork otherwise leaves alone. What it returns is
   GLIBC_PRIVATE, so the loader and the libc beside it are always built together and this shape can
   change without anybody's ABI moving.  */

#ifndef _DL_MINST_BUNDLE_H
#define _DL_MINST_BUNDLE_H 1

#include <stdint.h>

/* Where one image sits inside the artifact, and where it shows through.

   One of these per mount rather than per image, because what libc does with them is mount each one:
   an image mounted at two places is two of these naming the same bytes.  */
struct minst_bundle_image
{
  /* From the start of the artifact. */
  uint64_t offset;
  uint64_t length;

  /* Absolute, in the process's view. */
  const char *mount_point;

  /* Absolute, within the image. */
  const char *image_path;
};

struct minst_bundle_view
{
  /* The artifact, open. Not owned by libc: the loader keeps it for the life of the process, and
     reading through it is all libc does with it.  */
  int descriptor;

  const struct minst_bundle_image *images;
  uint32_t image_count;
};

/* What this process is running out of, or NULL when it is not running out of anything -- an ordinary
   program on an ordinary machine, which is the common case and not an error.

   Valid for the life of the process, and not the caller's to free.  */
extern const struct minst_bundle_view *_dl_minst_bundle_view (void);
rtld_hidden_proto (_dl_minst_bundle_view)

#endif
