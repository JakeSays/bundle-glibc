# Copyright (c) 2026, Jake Helfert
#
# SPDX-License-Identifier: MIT

# The bundle filesystem: reading an artifact's contents from inside libc.
#
# The sources are not here. They live in the bundlefs repository, carried as a submodule, so that the
# reader compiled into libc and the reader an application links in host mode are the same code rather
# than two copies that drift. This directory is the description of how glibc builds them.
#
# Only the read side. The half that builds images runs in a build tool and has no business in a
# libc, which is what BUNDLEFS_READER_ONLY selects for cmake and what this list selects here.

subdir := bundlefs

include ../Makeconfig

bundlefs-dir := $(..)third_party/bundlefs
erofs-dir := $(bundlefs-dir)/third_party/erofs-utils

# Reached the way elf/ reaches ../locale/programs.
vpath %.c $(bundlefs-dir)/lib
vpath %.c $(bundlefs-dir)/runtime
vpath %.c $(erofs-dir)/lib

# The reader, from the submodule.
bundlefs-routines := \
  BfsDirectory \
  BfsFile \
  BfsFileSystem \
  BfsImage \
  ErofsWriteStubs \
  # bundlefs-routines

# The libc side, also from the submodule: mounting what the app's view note describes, and the
# descriptor table these wrappers are routed through. It was written here, when glibc was the only
# libc there was; musl needs the same thing and it is now one copy rather than two.
runtime-routines := \
  BfsDescriptors \
  BfsRuntime \
  # runtime-routines

# What is genuinely glibc's, and all that is left of it: raw syscalls, one lock, errno, the page size
# and the two stat fills. Everything the shared half needs of those arrives through
# BfsPlatformInterface.
libc-routines := \
  BfsPlatform \
  # libc-routines

# Vendored, and the smallest set that reads an image. Everything else in erofs-utils is reachable
# from these by name without being reachable by execution -- super.c holds erofs_writesb beside
# erofs_read_superblock, data.c mentions the packed fragment inode -- and following those references
# would put the whole of mkfs into libc: 190KB of text and 12MB of zeroed data on a path that never
# runs. ErofsWriteStubs stands in for them instead.
erofs-routines := \
  config \
  data \
  decompress \
  dir \
  io \
  namei \
  super \
  zmap \
  # erofs-routines

routines := $(bundlefs-routines) $(erofs-routines) $(runtime-routines) $(libc-routines)

# What the vendored sources need in order to compile at all: their own headers, the private ones in
# their lib directory that the stubs reach for, and the configuration autoconf would have written.
erofs-cflags := \
  -I$(erofs-dir)/include \
  -I$(erofs-dir)/lib \
  -include $(..)bundlefs/ErofsConfig.h \
  -D_GNU_SOURCE

$(foreach r,$(erofs-routines),$(eval CFLAGS-$(r).c += $(erofs-cflags)))
$(foreach r,$(bundlefs-routines),$(eval CFLAGS-$(r).c += $(erofs-cflags) -I$(bundlefs-dir)/lib/include))

# Ours needs the reader's public headers, and nothing of erofs: it talks to the library rather than
# past it, which is the same boundary a payload sees. The runtime half needs its own published
# headers as well, and the bundle format, which the tree's CPPFLAGS already reaches.
bfs-include := -I$(bundlefs-dir)/lib/include -I$(bundlefs-dir)/runtime/include

$(foreach r,$(runtime-routines),$(eval CFLAGS-$(r).c += $(bfs-include)))
CFLAGS-BfsPlatform.c += $(bfs-include)

# erofs declares erofs_get_configure() with an empty parameter list, which in C means unspecified
# rather than none. The declaration is in a header everything includes, so every source needs the
# first of these; only the definition needs the second.
#
# Suppressed rather than patched: the fix belongs upstream, and carrying a patch against a submodule
# to silence two warnings would outlive the warnings.
$(foreach r,$(routines),$(eval CFLAGS-$(r).c += -Wno-strict-prototypes))
CFLAGS-config.c += -Wno-old-style-definition

include ../Rules
