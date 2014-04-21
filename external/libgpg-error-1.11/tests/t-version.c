/* t-version.c - Check the version info function
 * Copyright (C) 2013 g10 Code GmbH
 *
 * This file is part of libgpg-error.
 *
 * libgpg-error is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * libgpg-error is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../src/gpg-error.h"

static const char *logpfx = "";
static int verbose;
static int debug;
static int errorcount;

int
main (int argc, char **argv)
{
  int last_argc = -1;

  if (argc)
    {
      logpfx = *argv;
      argc--; argv++;
    }
  while (argc && last_argc != argc )
    {
      last_argc = argc;
      if (!strcmp (*argv, "--help"))
        {
          puts (
"usage: ./version [options]\n"
"\n"
"Options:\n"
"  --verbose      Show what is going on\n"
);
          exit (0);
        }
      if (!strcmp (*argv, "--verbose"))
        {
          verbose = 1;
          argc--; argv++;
        }
      else if (!strcmp (*argv, "--debug"))
        {
          verbose = debug = 1;
          argc--; argv++;
        }
    }

  if (!gpg_error_check_version (GPG_ERROR_VERSION))
    {
      fprintf (stderr, "%s: gpg_error_check_version returned an error\n",
               logpfx);
      errorcount++;
    }
  if (!gpg_error_check_version ("1.10"))
    {
      fprintf (stderr, "%s: gpg_error_check_version returned an "
               "error for an old version\n", logpfx);
      errorcount++;
    }
  if (gpg_error_check_version ("15"))
    {
      fprintf (stderr, "gpg_error_check_version did not return an error"
               " for a newer version\n", logpfx);
      errorcount++;
    }
  if (verbose || errorcount)
    {
      printf ("Version from header: %s (0x%06x)\n",
               GPG_ERROR_VERSION, GPG_ERROR_VERSION_NUMBER);
      printf ("Version from binary: %s\n", gpg_error_check_version (NULL));
      printf ("Copyright blurb ...:%s\n", gpg_error_check_version ("\x01\x01"));
    }

  return errorcount ? 1 : 0;
}
