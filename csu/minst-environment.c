/*
 * Copyright (c) 2026, Jake Helfert
 *
 * SPDX-License-Identifier: MIT
 */

/* What the artifact tells the program, applied before the program's constructors run.

   A program was built to be told things through the environment, and one that cannot be told
   anything has to be told from outside -- which is the one way a single file stops being one. Qt
   finding its own platform plugins is the case that forced this: the path it scans is a mount inside
   the artifact, and nothing else can name it.

   Here rather than in the loader, for two reasons. The loader has no allocator worth the name and
   setenv grows an array; and the environment libc hands the program does not exist until libc's own
   constructor assigns it, so anything the loader wrote would be overwritten. This runs immediately
   after that assignment and before any other object's constructor, which is the first moment the
   answer is both settable and observable.

   Nothing here fails loudly. An artifact that could not say what it wanted said is a program that
   will not find something, and it reports that in its own terms; a message from libc at this point
   would arrive before the program has said anything at all.  */

#include <dl-minst-bundle.h>
#include <minst/minst_view.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Whether LIST, split on SEPARATOR, has an entry equal to VALUE.  */
static bool
minst_contains (const char *list, const char *value, const char *separator)
{
  size_t separator_length = strlen (separator);
  size_t value_length = strlen (value);
  const char *at = list;

  for (;;)
    {
      const char *end = strstr (at, separator);
      size_t length = end != NULL ? (size_t) (end - at) : strlen (at);

      if (length == value_length && memcmp (at, value, length) == 0)
	return true;

      if (end == NULL)
	return false;

      at = end + separator_length;
    }
}

/* LIST with every entry equal to VALUE taken out, newly allocated, or NULL if there was no memory.

   Removal is what makes the list forms idempotent: prepending a path that is already somewhere in
   the list has to leave it in one place rather than two, or a variable an artifact adds to twice
   grows without bound.

   Never longer than what it was given, since entries only leave, so one allocation the size of the
   original is enough.  */
static char *
minst_without (const char *list, const char *value, const char *separator)
{
  size_t separator_length = strlen (separator);
  size_t value_length = strlen (value);

  char *result = malloc (strlen (list) + 1);

  if (result == NULL)
    return NULL;

  size_t written = 0;
  const char *at = list;

  for (;;)
    {
      const char *end = strstr (at, separator);
      size_t length = end != NULL ? (size_t) (end - at) : strlen (at);

      if (length != value_length || memcmp (at, value, length) != 0)
	{
	  if (written != 0)
	    {
	      memcpy (result + written, separator, separator_length);
	      written += separator_length;
	    }

	  memcpy (result + written, at, length);
	  written += length;
	}

      if (end == NULL)
	break;

      at = end + separator_length;
    }

  result[written] = '\0';

  return result;
}

static void
minst_apply (const struct minst_view_environment *action, const char *strings)
{
  const char *name = strings + action->name;
  const char *value = strings + action->value;
  const char *separator = strings + action->separator;
  const char *current = getenv (name);

  if (action->action == MINST_ENV_SET)
    {
      /* Something already said what this is, and the record asked to be second. */
      if ((action->flags & MINST_ENV_KEEP_EXISTING) != 0 && current != NULL)
	return;

      __setenv (name, value, 1);
      return;
    }

  if (action->action == MINST_ENV_UNSET)
    {
      __unsetenv (name);
      return;
    }

  const char *list = current != NULL ? current : "";

  /* The entry is in the list already and the record asked to be second, so it keeps the position it
     has rather than moving to the end this action names. About the one entry and not the list: the
     rest of the list is left alone either way, and a value that is not there is still added.  */
  if ((action->flags & MINST_ENV_KEEP_EXISTING) != 0
      && action->action != MINST_ENV_LIST_REMOVE
      && minst_contains (list, value, separator))
    return;

  char *rest = minst_without (list, value, separator);

  if (rest == NULL)
    return;

  if (action->action == MINST_ENV_LIST_REMOVE)
    {
      /* A list with no entries left is not an empty list, it is no list: an empty PATH or
	 LD_LIBRARY_PATH means something to what reads it, and it is not what taking the last entry
	 out was asking for.  */
      if (rest[0] == '\0')
	__unsetenv (name);
      else
	__setenv (name, rest, 1);

      free (rest);
      return;
    }

  size_t value_length = strlen (value);
  size_t rest_length = strlen (rest);
  size_t separator_length = strlen (separator);

  char *joined = malloc (value_length + separator_length + rest_length + 1);

  if (joined == NULL)
    {
      free (rest);
      return;
    }

  if (rest_length == 0)
    {
      memcpy (joined, value, value_length + 1);
    }
  else if (action->action == MINST_ENV_LIST_PREPEND)
    {
      memcpy (joined, value, value_length);
      memcpy (joined + value_length, separator, separator_length);
      memcpy (joined + value_length + separator_length, rest, rest_length + 1);
    }
  else
    {
      memcpy (joined, rest, rest_length);
      memcpy (joined + rest_length, separator, separator_length);
      memcpy (joined + rest_length + separator_length, value, value_length + 1);
    }

  __setenv (name, joined, 1);

  free (joined);
  free (rest);
}

void
__minst_apply_environment (void)
{
  const struct minst_bundle_view *view = _dl_minst_bundle_view ();

  /* No artifact around us, which is the common case and not an error. */
  if (view == NULL || view->environment == NULL)
    return;

  /* In the order the manifest gave them, so that an unset followed by a list-prepend reads the way
     it looks.  */
  for (uint32_t index = 0; index < view->environment_count; ++index)
    minst_apply (&view->environment[index], view->strings);
}
