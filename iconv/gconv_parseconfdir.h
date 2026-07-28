/* Handle configuration data.
   Copyright (C) 2021-2026 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <https://www.gnu.org/licenses/>.  */

#include <dirent.h>
#include <libc-symbols.h>
#include <locale.h>
#include <sys/types.h>

/* For GLRO (dl_minst_open_member).  Guarded for the same reason its use below is: only a shared
   build has a loader to ask.  */
#ifdef SHARED
# include <ldsodefs.h>
#endif

#if IS_IN (libc)
# include <libio/libioP.h>
# define __getdelim(line, len, c, fp) __getdelim (line, len, c, fp)

# undef isspace
# define isspace(__c) __isspace_l ((__c), _nl_C_locobj_ptr)
# define asprintf __asprintf
# define opendir __opendir
# define readdir64 __readdir64
# define closedir __closedir
# define mempcpy __mempcpy
# define struct_stat64 struct __stat64_t64
# define stat64_impl __stat64_time64
# define feof_unlocked __feof_unlocked
#else
# define stat64_impl stat64
# define struct_stat64 struct stat64
#endif

/* Name of the file containing the module information in the directories
   along the path.  */
static const char gconv_conf_filename[] = "gconv-modules";

static void add_alias (char *);
static void add_module (char *, const char *, size_t, int);

/* Read the next configuration file.  */
static bool
read_conf_file (const char *filename, const char *directory, size_t dir_len)
{
  /* The artifact first, if this is running inside one.  FILENAME was built by joining a directory
     compiled in when this runtime was configured to a name -- and that directory describes a machine
     the artifact is not running on, so the file it names is the host's or nothing.  What the artifact
     carries is looked for by the tail of that path, the same way a gconv module is.

     Falls through to the ordinary open when there is no artifact, or when there is one and it does
     not carry this file: an artifact that is not sealed is entitled to the host's configuration.  */
  FILE *fp = NULL;

#ifdef SHARED
  if (GLRO (dl_minst_open_member) != NULL)
    {
      int fd = GLRO (dl_minst_open_member) (filename);
      if (fd >= 0)
	{
	  fp = fdopen (fd, "rc");
	  if (fp == NULL)
	    __close (fd);
	}
    }
#endif

  /* Note the file is opened with cancellation in the I/O functions
     disabled.  */
  if (fp == NULL)
    fp = fopen (filename, "rce");

  char *line = NULL;
  size_t line_len = 0;
  static int modcounter;

  /* Don't complain if a file is not present or readable, simply silently
     ignore it.  */
  if (fp == NULL)
    return false;

  /* No threads reading from this stream.  */
  __fsetlocking (fp, FSETLOCKING_BYCALLER);

  /* Process the known entries of the file.  Comments start with `#' and
     end with the end of the line.  Empty lines are ignored.  */
  while (!feof_unlocked (fp))
    {
      char *rp, *endp, *word;
      ssize_t n = __getdelim (&line, &line_len, '\n', fp);
      if (n < 0)
	/* An error occurred.  */
	break;

      rp = line;
      /* Terminate the line (excluding comments or newline) by an NUL byte
	 to simplify the following code.  */
      endp = strchr (rp, '#');
      if (endp != NULL)
	*endp = '\0';
      else
	if (rp[n - 1] == '\n')
	  rp[n - 1] = '\0';

      while (isspace (*rp))
	++rp;

      /* If this is an empty line go on with the next one.  */
      if (rp == endp)
	continue;

      word = rp;
      while (*rp != '\0' && !isspace (*rp))
	++rp;

      if (rp - word == sizeof ("alias") - 1
	  && memcmp (word, "alias", sizeof ("alias") - 1) == 0)
	add_alias (rp);
      else if (rp - word == sizeof ("module") - 1
	       && memcmp (word, "module", sizeof ("module") - 1) == 0)
	add_module (rp, directory, dir_len, modcounter++);
      /* else */
	/* Otherwise ignore the line.  */
    }

  free (line);

  fclose (fp);
  return true;
}

/* Prefix DIR (with length DIR_LEN) with PREFIX if the latter is non-NULL and
   parse configuration in it.  */

static __always_inline bool
gconv_parseconfdir (const char *prefix, const char *dir, size_t dir_len)
{
  /* No slash needs to be inserted between dir and gconv_conf_filename; dir
     already ends in a slash.  */
  size_t buflen = dir_len + sizeof (gconv_conf_filename);
  char *buf = malloc (buflen + (prefix != NULL ? strlen (prefix) : 0));
  char *cp = buf;
  bool found = false;

  if (buf == NULL)
    return false;

  if (prefix != NULL)
    cp = stpcpy (cp, prefix);

  mempcpy (mempcpy (cp, dir, dir_len), gconv_conf_filename,
	   sizeof (gconv_conf_filename));

  /* One configuration file, not a file and a directory of more.

     Stock glibc also lists gconv-modules.d and reads every .conf in it.  That is a directory listing
     on a path built from a prefix compiled in when this runtime was configured -- a directory on
     somebody else's machine as far as an artifact is concerned, and a way for what an artifact
     converts to depend on what is installed where it happens to run.

     Nothing is lost by dropping it.  The bundler joins those files onto this one when it assembles
     the runtime bundle, so what arrives here is everything that was in both, and it does that for a
     member set supplied through --replace-glibc as readily as for the built-in one.  */
  found = read_conf_file (buf, dir, dir_len);

  free (buf);
  return found;
}
