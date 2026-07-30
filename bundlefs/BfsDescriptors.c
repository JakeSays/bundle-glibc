/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* The descriptors libc hands out on paths the artifact carries.
 *
 * Read through bundlefs where they lie rather than copied into an anonymous file. The table here is
 * what that costs; not materialising every byte of every member anything opens is what it buys.  */

#include <bundlefs/BundleFs.h>
#include <bundlefs-descriptors.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libc-lock.h>
#include <not-cancel.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sysdep.h>

/* What a number points at, kept apart from the number itself because the kernel keeps them apart.
   Several descriptors can name one of these -- that is exactly what dup makes -- and they share the
   read position, because the position lives in the BfsFile they all reach.

   A description is on a file or on a directory, never both. Which it is decides what every routed
   call does with the number.  */
struct bfs_description
{
  BfsFile *file;
  BfsDirectory *directory;

  /* Taken when the description is made, so that fstat can answer without asking again -- and so that
     it can answer at all for a directory, which has nothing to ask.  */
  BfsFileInfo info;

  /* What the caller opened with. The number underneath is an anonymous file the kernel made
     read-write, so it cannot answer F_GETFL for the file the caller thinks it has.  */
  int flags;

  /* The path this was opened by, owned here. openat and its relatives take a path relative to a
     descriptor, and answering that means knowing where the descriptor is -- which the kernel cannot
     say, since the number it holds is an anonymous file with no name in any tree.  */
  char *path;

  /* How many numbers name this. The file is let go when the last one does.  */
  uint32_t references;
};

/* Grown rather than fixed, because a program that opens a hundred bundled files is doing nothing
   unreasonable. Small to begin with because most processes open none.

   One table for files and directories alike: every routed call has to ask about a number without
   knowing yet which it is, and two tables would mean two lookups to find that out.  */
struct bfs_descriptor
{
  int fd;
  struct bfs_description *description;
};

static struct bfs_descriptor *descriptors;
static size_t descriptor_count;
static size_t descriptor_capacity;

__libc_lock_define_initialized (static, descriptor_lock);

/* What a number points at, with the lock held by the caller.  */
static struct bfs_description *
describe_locked (int fd)
{
  for (size_t i = 0; i < descriptor_count; ++i)
    if (descriptors[i].fd == fd)
      return descriptors[i].description;

  return NULL;
}

bool
__bfs_owns (int fd)
{
  if (descriptor_count == 0 || fd < 0)
    return false;

  __libc_lock_lock (descriptor_lock);

  bool found = describe_locked (fd) != NULL;

  __libc_lock_unlock (descriptor_lock);

  return found;
}

/* The file behind a descriptor, with the lock held by the caller. Null for a directory, which is
   what every caller of this wants to hear about one.  */
static BfsFile *
find_locked (int fd)
{
  struct bfs_description *description = describe_locked (fd);

  return description == NULL ? NULL : description->file;
}

static BfsDirectory *
find_directory_locked (int fd)
{
  struct bfs_description *description = describe_locked (fd);

  return description == NULL ? NULL : description->directory;
}

/* Lets go of one reference, closing the file when it was the last. The lock is held by the caller.  */
static void
release_locked (struct bfs_description *description)
{
  if (description == NULL || --description->references != 0)
    return;

  if (description->file != NULL)
    BfsFileClose (description->file);
  else
    BfsDirectoryClose (description->directory);

  free (description->path);
  free (description);
}

/* Drops the table's entry for a number, if it has one. The lock is held by the caller.  */
static void
drop_locked (int fd)
{
  for (size_t i = 0; i < descriptor_count; ++i)
    if (descriptors[i].fd == fd)
      {
	release_locked (descriptors[i].description);

	descriptors[i] = descriptors[descriptor_count - 1];
	--descriptor_count;
	return;
      }
}

/* Adds a number naming an existing description. The lock is held by the caller, and the caller has
   already counted the reference.  */
static bool
attach_locked (int fd, struct bfs_description *description)
{
  if (descriptor_count == descriptor_capacity)
    {
      size_t wanted = descriptor_capacity == 0 ? 8 : descriptor_capacity * 2;
      struct bfs_descriptor *grown
	= realloc (descriptors, wanted * sizeof (*grown));

      if (grown == NULL)
	return false;

      descriptors = grown;
      descriptor_capacity = wanted;
    }

  descriptors[descriptor_count].fd = fd;
  descriptors[descriptor_count].description = description;
  ++descriptor_count;

  return true;
}

/* Records a newly opened file or directory as ours, under a number of its own.  */
static bool
remember (int fd, BfsFile *file, BfsDirectory *directory, const BfsFileInfo *info, int flags,
	  const char *path)
{
  struct bfs_description *description = malloc (sizeof (*description));
  if (description == NULL)
    return false;

  size_t length = strlen (path);

  description->path = malloc (length + 1);
  if (description->path == NULL)
    {
      free (description);
      return false;
    }

  memcpy (description->path, path, length + 1);

  description->file = file;
  description->directory = directory;
  description->info = *info;
  description->flags = flags;
  description->references = 1;

  __libc_lock_lock (descriptor_lock);

  /* A number the table already knows means one was closed without coming through here, and the
     kernel has since handed it out again. Every lookup stops at the first match, so appending would
     shadow the new file behind the dead one -- let the old one go first.  */
  drop_locked (fd);

  bool added = attach_locked (fd, description);

  __libc_lock_unlock (descriptor_lock);

  if (!added)
    {
      free (description->path);
      free (description);
    }

  return added;
}

bool
__bfs_adopt (int from, int to)
{
  __libc_lock_lock (descriptor_lock);

  struct bfs_description *description = describe_locked (from);

  /* Counted before TO is let go, not after. dup2 onto its own number is legal, and there FROM and TO
     are the same entry -- dropping first would free the description this is about to share.  */
  if (description != NULL)
    ++description->references;

  /* Whatever TO named before is let go either way, exactly as the kernel closes the number it is
     about to reuse. When FROM is not ours, that is the whole of the work: TO stops being ours.  */
  drop_locked (to);

  bool added = true;

  if (description != NULL)
    {
      added = attach_locked (to, description);
      if (!added)
	release_locked (description);
    }

  __libc_lock_unlock (descriptor_lock);

  return added;
}

void
__bfs_forget (int fd)
{
  __libc_lock_lock (descriptor_lock);

  drop_locked (fd);

  __libc_lock_unlock (descriptor_lock);
}

void
__bfs_forget_range (unsigned int first, unsigned int last)
{
  __libc_lock_lock (descriptor_lock);

  /* Backwards over the table rather than forwards over the range, which may be everything up to
     INT_MAX. drop_locked fills the hole from the end, so walking down visits each entry once.  */
  for (size_t i = descriptor_count; i >= 1; --i)
    {
      int fd = descriptors[i - 1].fd;

      if ((unsigned int) fd >= first && (unsigned int) fd <= last)
	drop_locked (fd);
    }

  __libc_lock_unlock (descriptor_lock);
}

/* An empty anonymous file that can never be written to, which is what a descriptor of ours is.
 *
 * A real descriptor, so the number cannot collide with one the kernel gave out and it behaves as a
 * descriptor should when it is duplicated, inherited or closed. What it carries is its own existence:
 * the content stays in the image.
 *
 * Sealed rather than merely refused by the routed calls. Those return EBADF, which is the errno a
 * program expects from writing to something it opened read-only, but they only cover callers that
 * come through them -- a raw syscall, another libc in the process, an interposed write. The seal is
 * the kernel holding the line for all of them, and it is permanent once set.  */
static int
sealed_token (const char *path)
{
  int fd = INLINE_SYSCALL_CALL (memfd_create, path, MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0)
    return -1;

  /* Growing is sealed too, or the file could be extended and the new bytes written.
     __fcntl64_nocancel rather than the public one, so this is not something a payload can interpose
     between the descriptor being made and being closed to writing.  */
  if (__fcntl64_nocancel (fd, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK) != 0)
    {
      __close_nocancel (fd);
      return -1;
    }

  return fd;
}

/* Defined below, beside opendir, which is its other caller.  */
static int open_directory (BfsFileSystem *fs, const char *path, const BfsFileInfo *info, int flags);

int
__bfs_open_path (const char *path, int flags)
{
  BfsFileSystem *fs = BfsProcessFileSystem ();
  if (fs == NULL || path == NULL)
    return -1;

  /* Anything that writes, creates, truncates or appends is about the machine's filesystem. An image
     is read-only and there is nothing here to write into.  */
  if ((flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0)
    return -1;

  /* A directory reached by open rather than by opendir, which is how openat and fdopendir get one.
     BfsFileOpen would refuse it, and refusing here would send the caller to the kernel for a path
     the machine does not have.  */
  BfsFileInfo probe;
  if (BfsFileSystemStat (fs, path, &probe) == 0 && probe.Kind == BfsEntryKindDirectory)
    return open_directory (fs, path, &probe, flags);

  BfsFile *file = NULL;
  if (BfsFileOpen (fs, path, &file) != 0)
    return -1;

  BfsFileInfo info;
  if (BfsFileInformation (file, &info) != 0)
    {
      BfsFileClose (file);
      return -1;
    }

  int fd = sealed_token (path);
  if (fd < 0)
    {
      BfsFileClose (file);
      return -1;
    }

  if (!remember (fd, file, NULL, &info, flags, path))
    {
      __close_nocancel (fd);
      BfsFileClose (file);
      return -1;
    }

  return fd;
}

/* Filling a struct stat from what the image knows, shared by the path and descriptor forms.
 *
 * No device or inode: those have to mean something in this process's world and an image has no basis
 * for inventing them, which bundlefs says itself rather than making something up. What a caller
 * usually wants them for -- telling two files apart -- is answered by the descriptor's own identity
 * where there is one.  */
static void
fill_stat (const BfsFileInfo *info, struct stat64 *buffer)
{
  memset (buffer, 0, sizeof (*buffer));

  unsigned int kind;

  switch (info->Kind)
    {
    case BfsEntryKindDirectory:
      kind = S_IFDIR;
      break;

    case BfsEntryKindSymbolicLink:
      kind = S_IFLNK;
      break;

    default:
      kind = S_IFREG;
      break;
    }

  buffer->st_mode = kind | (info->Mode & 07777);
  buffer->st_size = (off64_t) info->Size;
  buffer->st_uid = info->UserId;
  buffer->st_gid = info->GroupId;
  buffer->st_mtime = (time_t) info->ModificationTime;
  buffer->st_atime = (time_t) info->ModificationTime;
  buffer->st_ctime = (time_t) info->ModificationTime;
  buffer->st_nlink = 1;
  buffer->st_blksize = 4096;
  buffer->st_blocks = (blkcnt64_t) ((info->Size + 511) / 512);
}

/* Filling a struct statx, which asks what it wants by mask and is told what it got the same way.
 *
 * The image knows nothing about a device or inode meaning anything in this process, so those bits
 * are left out of stx_mask rather than filled with something invented. A caller that asked for them
 * sees they were not answered, which is what the mask is for.  */
static void
fill_statx (const BfsFileInfo *info, struct statx *buffer)
{
  memset (buffer, 0, sizeof (*buffer));

  unsigned int kind;

  switch (info->Kind)
    {
    case BfsEntryKindDirectory:
      kind = S_IFDIR;
      break;

    case BfsEntryKindSymbolicLink:
      kind = S_IFLNK;
      break;

    default:
      kind = S_IFREG;
      break;
    }

  buffer->stx_mask = STATX_TYPE | STATX_MODE | STATX_NLINK | STATX_UID | STATX_GID | STATX_SIZE
		     | STATX_ATIME | STATX_MTIME | STATX_CTIME;
  buffer->stx_blksize = 4096;
  buffer->stx_nlink = 1;
  buffer->stx_uid = info->UserId;
  buffer->stx_gid = info->GroupId;
  buffer->stx_mode = kind | (info->Mode & 07777);
  buffer->stx_size = info->Size;
  buffer->stx_blocks = (info->Size + 511) / 512;

  buffer->stx_atime.tv_sec = (int64_t) info->ModificationTime;
  buffer->stx_mtime.tv_sec = (int64_t) info->ModificationTime;
  buffer->stx_ctime.tv_sec = (int64_t) info->ModificationTime;
}

int
__bfs_statx_path (const char *path, struct statx *buffer)
{
  BfsFileSystem *fs = BfsProcessFileSystem ();
  if (fs == NULL || path == NULL)
    return -1;

  BfsFileInfo info;
  if (BfsFileSystemStat (fs, path, &info) != 0)
    return -1;

  fill_statx (&info, buffer);

  return 0;
}

int
__bfs_statx_fd (int fd, struct statx *buffer)
{
  __libc_lock_lock (descriptor_lock);

  struct bfs_description *description = describe_locked (fd);

  BfsFileInfo info;
  bool found = description != NULL;

  if (found)
    info = description->info;

  __libc_lock_unlock (descriptor_lock);

  if (!found)
    return -1;

  fill_statx (&info, buffer);

  return 0;
}

int
__bfs_stat_path (const char *path, struct stat64 *buffer)
{
  BfsFileSystem *fs = BfsProcessFileSystem ();
  if (fs == NULL || path == NULL)
    return -1;

  BfsFileInfo info;
  if (BfsFileSystemStat (fs, path, &info) != 0)
    return -1;

  fill_stat (&info, buffer);

  return 0;
}

bool
__bfs_resolve_at (int fd, const char *path, char *buffer, size_t length)
{
  if (path == NULL)
    return false;

  /* An absolute path ignores the descriptor entirely, which is what openat and its relatives are
     defined to do. AT_FDCWD leaves the path as it stands; a relative one is not something an image
     carries, and the lookup that follows will say so.  */
  if (path[0] == '/' || fd == AT_FDCWD)
    {
      size_t wanted = strlen (path);
      if (wanted >= length)
	return false;

      memcpy (buffer, path, wanted + 1);

      return true;
    }

  __libc_lock_lock (descriptor_lock);

  struct bfs_description *description = describe_locked (fd);

  /* Only a directory of ours can root a relative path. A file cannot, and a descriptor the table
     does not know is the machine's -- so is anything under it.  */
  bool rooted = description != NULL && description->directory != NULL;
  size_t used = 0;

  if (rooted)
    {
      size_t root = strlen (description->path);
      size_t rest = strlen (path);

      /* One separator, unless the mount point is the root and already is one.  */
      bool separate = root > 0 && description->path[root - 1] != '/';

      used = root + (separate ? 1 : 0) + rest;

      if (used >= length)
	{
	  rooted = false;
	}
      else
	{
	  memcpy (buffer, description->path, root);

	  if (separate)
	    buffer[root++] = '/';

	  memcpy (buffer + root, path, rest + 1);
	}
    }

  __libc_lock_unlock (descriptor_lock);

  return rooted;
}

bool
__bfs_carries (const char *path)
{
  BfsFileSystem *fs = BfsProcessFileSystem ();
  if (fs == NULL || path == NULL)
    return false;

  BfsFileInfo info;

  return BfsFileSystemStat (fs, path, &info) == 0;
}

/* A descriptor on a carried directory. Shared by opendir and by a plain open of a directory, which
   are the same thing under different names -- openat and fdopendir both arrive by the second.  */
static int
open_directory (BfsFileSystem *fs, const char *path, const BfsFileInfo *info, int flags)
{
  BfsDirectory *directory = NULL;
  if (BfsDirectoryOpen (fs, path, &directory) != 0)
    return -1;

  /* A number and nothing else, exactly as a file gets: the entries stay in the image until something
     asks for them.  */
  int fd = sealed_token (path);
  if (fd < 0)
    {
      BfsDirectoryClose (directory);
      return -1;
    }

  if (!remember (fd, NULL, directory, info, flags, path))
    {
      __close_nocancel (fd);
      BfsDirectoryClose (directory);
      return -1;
    }

  return fd;
}

int
__bfs_opendir_fd (const char *path)
{
  BfsFileSystem *fs = BfsProcessFileSystem ();
  if (fs == NULL || path == NULL)
    return -1;

  /* Asked of the filesystem rather than of the directory: a BfsDirectory is a cursor over names and
     has nothing to say about itself, and opendir stats the descriptor before it will use it.  */
  BfsFileInfo info;
  if (BfsFileSystemStat (fs, path, &info) != 0)
    return -1;

  if (info.Kind != BfsEntryKindDirectory)
    return -1;

  /* opendir asked for these itself; it is the only caller and it always opens for reading.  */
  return open_directory (fs, path, &info, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}

/* getdents64, served straight out of the image.
 *
 * The entries are written into the caller's own buffer as they are read, which is what
 * BfsDirectoryEntry is shaped for -- its name is a pointer into the reader's storage rather than a
 * copy, so nothing is duplicated on the way past.  */
ssize_t
__bfs_getdents64 (int fd, void *buffer, size_t length)
{
  __libc_lock_lock (descriptor_lock);

  BfsDirectory *directory = find_directory_locked (fd);
  if (directory == NULL)
    {
      __libc_lock_unlock (descriptor_lock);
      return -1;
    }

  char *cursor = buffer;
  size_t used = 0;

  while (true)
    {
      BfsDirectoryEntry entry;

      int got = BfsDirectoryRead (directory, &entry);
      if (got < 0)
	{
	  __libc_lock_unlock (descriptor_lock);
	  __set_errno (EIO);
	  return -1;
	}

      /* The end of the directory.  */
      if (got == 0)
	break;

      size_t record = offsetof (struct dirent64, d_name) + entry.NameLength + 1;
      record = (record + 7) & ~(size_t) 7;

      /* No room for this one, so it is left for the next call -- which means seeking back to where
	 it began, since it has already been read.  */
      if (used + record > length)
	{
	  BfsDirectorySeek (directory, entry.NextPosition - 1);
	  break;
	}

      struct dirent64 *packed = (struct dirent64 *) (cursor + used);

      memset (packed, 0, record);

      /* An inode number the image has no basis for, and one a caller can still tell apart within this
	 directory, which is what it is nearly always used for.  */
      packed->d_ino = (ino64_t) entry.NextPosition;
      packed->d_off = (off64_t) entry.NextPosition;
      packed->d_reclen = (unsigned short) record;

      switch (entry.Kind)
	{
	case BfsEntryKindDirectory:
	  packed->d_type = DT_DIR;
	  break;

	case BfsEntryKindSymbolicLink:
	  packed->d_type = DT_LNK;
	  break;

	default:
	  packed->d_type = DT_REG;
	  break;
	}

      memcpy (packed->d_name, entry.Name, entry.NameLength);

      used += record;
    }

  __libc_lock_unlock (descriptor_lock);

  return (ssize_t) used;
}

ssize_t
__bfs_readlink_path (const char *path, char *buffer, size_t length)
{
  BfsFileSystem *fs = BfsProcessFileSystem ();
  if (fs == NULL || path == NULL)
    return -1;

  int64_t got = BfsFileSystemReadLink (fs, path, buffer, length);

  return got < 0 ? -1 : (ssize_t) got;
}

ssize_t
__bfs_read (int fd, void *buffer, size_t length)
{
  __libc_lock_lock (descriptor_lock);

  BfsFile *file = find_locked (fd);
  int64_t got = file == NULL ? -1 : BfsFileRead (file, buffer, length);

  __libc_lock_unlock (descriptor_lock);

  if (file == NULL)
    return -1;

  if (got < 0)
    {
      __set_errno (EIO);
      return -1;
    }

  return (ssize_t) got;
}

ssize_t
__bfs_pread (int fd, void *buffer, size_t length, off_t offset)
{
  __libc_lock_lock (descriptor_lock);

  BfsFile *file = find_locked (fd);
  int64_t got = file == NULL ? -1 : BfsFileReadAt (file, (uint64_t) offset, buffer, length);

  __libc_lock_unlock (descriptor_lock);

  if (file == NULL)
    return -1;

  if (got < 0)
    {
      __set_errno (EIO);
      return -1;
    }

  return (ssize_t) got;
}

/* readv and preadv, filling the buffers in order.
 *
 * One pass under one lock rather than a loop of __bfs_read outside it, so the position cannot move
 * between buffers -- which it could, since a duplicated number shares it.
 *
 * A short fill ends the call, exactly as the syscalls do: what has been read is returned and the
 * position is left after it.  */
static ssize_t
read_vector (int fd, const struct iovec *vector, int count, uint64_t offset, bool positional)
{
  __libc_lock_lock (descriptor_lock);

  BfsFile *file = find_locked (fd);
  if (file == NULL)
    {
      __libc_lock_unlock (descriptor_lock);
      return -1;
    }

  ssize_t total = 0;
  bool failed = false;

  for (int i = 0; i < count; ++i)
    {
      if (vector[i].iov_len == 0)
	continue;

      int64_t got = positional
	? BfsFileReadAt (file, offset + (uint64_t) total, vector[i].iov_base, vector[i].iov_len)
	: BfsFileRead (file, vector[i].iov_base, vector[i].iov_len);

      if (got < 0)
	{
	  failed = true;
	  break;
	}

      total += got;

      /* The end of the file, or less than was asked for, which ends the call either way.  */
      if ((size_t) got < vector[i].iov_len)
	break;
    }

  __libc_lock_unlock (descriptor_lock);

  if (failed && total == 0)
    {
      __set_errno (EIO);
      return -1;
    }

  return total;
}

ssize_t
__bfs_readv (int fd, const struct iovec *vector, int count)
{
  return read_vector (fd, vector, count, 0, false);
}

ssize_t
__bfs_preadv (int fd, const struct iovec *vector, int count, off_t offset)
{
  return read_vector (fd, vector, count, (uint64_t) offset, true);
}

/* sendfile from a carried file.
 *
 * The kernel cannot do this one: it copies between two descriptors it knows, and ours holds an empty
 * anonymous file. So the bytes come out of the image into a buffer and go to the output descriptor
 * from there -- the one place a copy is unavoidable on the read path, because the destination is
 * somebody else's file and the content has to cross.
 *
 * The write is left to the caller, which does it outside the table's lock.  */
ssize_t
__bfs_sendfile_read (int fd, void *buffer, size_t length, off_t *offset)
{
  __libc_lock_lock (descriptor_lock);

  BfsFile *file = find_locked (fd);
  int64_t got = -1;

  if (file != NULL)
    {
      got = offset == NULL
	? BfsFileRead (file, buffer, length)
	: BfsFileReadAt (file, (uint64_t) *offset, buffer, length);
    }

  __libc_lock_unlock (descriptor_lock);

  if (file == NULL)
    return -1;

  if (got < 0)
    {
      __set_errno (EIO);
      return -1;
    }

  return (ssize_t) got;
}

off_t
__bfs_lseek (int fd, off_t offset, int whence)
{
  BfsSeekOrigin origin;

  switch (whence)
    {
    case SEEK_SET:
      origin = BfsSeekOriginBeginning;
      break;

    case SEEK_CUR:
      origin = BfsSeekOriginCurrent;
      break;

    case SEEK_END:
      origin = BfsSeekOriginEnd;
      break;

    default:
      __set_errno (EINVAL);
      return -1;
    }

  __libc_lock_lock (descriptor_lock);

  BfsFile *file = find_locked (fd);
  int64_t where = file == NULL ? -1 : BfsFileSeek (file, offset, origin);

  __libc_lock_unlock (descriptor_lock);

  if (file == NULL)
    return -1;

  if (where < 0)
    {
      __set_errno (EINVAL);
      return -1;
    }

  return (off_t) where;
}

int
__bfs_flags (int fd)
{
  __libc_lock_lock (descriptor_lock);

  struct bfs_description *description = describe_locked (fd);
  int flags = description == NULL ? -1 : description->flags;

  __libc_lock_unlock (descriptor_lock);

  if (flags < 0)
    return -1;

  /* What F_GETFL reports and nothing else: the access mode and the file status flags. The rest of
     what open was given -- O_CLOEXEC, O_DIRECTORY, O_NOFOLLOW -- either belongs to the descriptor
     rather than the file description or is spent at open and never read back.

     O_LARGEFILE because Linux sets it on every 64-bit open, so a caller comparing what it gets here
     against what it would get from a file on the machine sees the same thing.  */
  return (flags & (O_ACCMODE | O_APPEND | O_ASYNC | O_DIRECT | O_NOATIME | O_NONBLOCK))
	 | O_LARGEFILE;
}

int
__bfs_fstat (int fd, struct stat64 *buffer)
{
  __libc_lock_lock (descriptor_lock);

  /* From what was taken when the description was made. A directory has nothing to ask afterwards, and
     opendir stats its descriptor before it will use it.  */
  struct bfs_description *description = describe_locked (fd);

  BfsFileInfo info;
  bool found = description != NULL;

  if (found)
    info = description->info;

  __libc_lock_unlock (descriptor_lock);

  if (!found)
    return -1;

  fill_stat (&info, buffer);

  return 0;
}

/* Copies the file into the descriptor and maps it from there.
 *
 * For the files that have no extent to map -- compressed, or with the tail packed into the inode --
 * and for a request reaching past the blocks the content occupies, where mapping the artifact would
 * hand the program the next file's bytes.
 *
 * Into a file of its own rather than into the caller's descriptor, which was sealed against writing
 * the moment it was made and cannot be filled afterwards. Nothing is lost by that: the descriptor is
 * a token and never needed content, and a mapping holds its own reference to what backs it, so the
 * temporary can be closed as soon as it has been mapped.
 *
 * It is sealed too, before it is mapped, so that a shared writable mapping is refused here exactly as
 * the read-only artifact refuses one on the ordinary path.  */
static void *
materialise (const char *name, BfsFile *file, uint64_t size, void *address, size_t length,
	     int protection, int flags, off64_t offset)
{
  int fd = INLINE_SYSCALL_CALL (memfd_create, name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0)
    return MAP_FAILED;

  /* The syscalls throughout rather than the libc entry points. Two reasons, and both are load
     bearing: the write side refuses anything in the table, and close is called here with the
     descriptor lock held -- __close_nocancel would reach __bfs_forget, which takes that same lock and
     would deadlock on the spot. Nothing here is in the table, so there is nothing to forget.  */
  if (INLINE_SYSCALL_CALL (ftruncate, fd, (off64_t) size) != 0)
    {
      INLINE_SYSCALL_CALL (close, fd);
      return MAP_FAILED;
    }

  char buffer[65536];
  uint64_t at = 0;

  while (at < size)
    {
      uint64_t want = size - at;
      if (want > sizeof (buffer))
	want = sizeof (buffer);

      int64_t got = BfsFileReadAt (file, at, buffer, want);
      if (got <= 0)
	{
	  INLINE_SYSCALL_CALL (close, fd);
	  __set_errno (EIO);
	  return MAP_FAILED;
	}

      ssize_t written = INLINE_SYSCALL_CALL (pwrite64, fd, buffer, (size_t) got,
					     SYSCALL_LL64_PRW ((off64_t) at));
      if (written != got)
	{
	  INLINE_SYSCALL_CALL (close, fd);
	  __set_errno (EIO);
	  return MAP_FAILED;
	}

      at += (uint64_t) got;
    }

  if (__fcntl64_nocancel (fd, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK) != 0)
    {
      INLINE_SYSCALL_CALL (close, fd);
      return MAP_FAILED;
    }

  void *mapped = (void *) INLINE_SYSCALL_CALL (mmap, address, length, protection, flags, fd, offset);

  /* The mapping keeps what it needs, so the number has done its job.  */
  INLINE_SYSCALL_CALL (close, fd);

  return mapped;
}

void *
__bfs_mmap (void *address, size_t length, int protection, int flags, int fd, off64_t offset)
{
  __libc_lock_lock (descriptor_lock);

  BfsFile *file = find_locked (fd);

  BfsExtent extent;
  int described = file == NULL ? -EBADF : BfsFileExtent (file, &extent);

  /* Taken under the lock with everything else, since the fallback needs it and reaching for the
     table twice would let the descriptor be closed in between.  */
  uint64_t size = 0;
  if (file != NULL)
    {
      BfsFileInfo info;
      if (BfsFileInformation (file, &info) == 0)
	size = info.Size;
    }

  void *result = MAP_FAILED;

  if (file == NULL)
    {
      /* Ours, since the caller asked before coming here, so this is a directory rather than a
	 number nobody knows -- and a directory is not something to map.  */
      __set_errno (ENODEV);
    }
  else if (described == 0
	   && (extent.Offset % (uint64_t) __getpagesize ()) == 0
	   && (uint64_t) offset + length <= extent.Span)
    {
      /* The artifact itself, at the offset the content lies at. Read-only, so the kernel refuses a
	 shared writable mapping on the program's behalf and a private one gets copy-on-write as it
	 would anywhere else.

	 The last block is only partly filled, and the padding is this file's own -- which is what
	 makes mapping a whole number of pages safe when the content is not.  */
      result = (void *) INLINE_SYSCALL_CALL (mmap, address, length, protection, flags,
					     extent.Descriptor,
					     extent.Offset + (uint64_t) offset);
    }
  else
    {
      /* Named so that a copy is visible for what it is in /proc/self/maps, where the ordinary path
	 shows the artifact instead.  */
      result = materialise ("minst-copy", file, size, address, length, protection, flags, offset);
    }

  __libc_lock_unlock (descriptor_lock);

  return result;
}
