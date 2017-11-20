/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Dieter Baron and Thomas Klausner.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _CUBRID_GETOPT_H_
#define _CUBRID_GETOPT_H_

#include "config.h"

#ifdef HAVE_GETOPT_H
#include <getopt.h>		/* for getopt_long */
#include <unistd.h>		/* for getopt */
#else
#if !defined(WINDOWS)
#include <sys/cdefs.h>
#define DllImport
#else
#define DllImport  __declspec(dllimport)
#endif

/*
 * Gnu like getopt_long() and BSD4.4 getsubopt()/optreset extensions
 */
#define no_argument        0
#define required_argument  1
#define optional_argument  2

struct option
{
  /* name of long option */
  const char *name;
  /*
   * one of no_argument, required_argument, and optional_argument:
   * whether option takes an argument
   */
  int has_arg;
  /* if not NULL, set *flag to val when option found */
  int *flag;
  /* if flag not NULL, value to set *flag to; else return value */
  int val;
};

#ifdef __cplusplus
extern "C"
{
#endif

  int getopt (int, char *const *, const char *);
  int getopt_long (int, char *const *, const char *, const struct option *, int *);

/* On some platforms, this is in libc, but not in a system header */
  extern DllImport int optreset;
  extern DllImport char *optarg;
  extern DllImport int opterr;
  extern DllImport int optind;
  extern DllImport int optopt;

#ifdef __cplusplus
}
#endif
#endif

typedef struct option GETOPT_LONG;

#endif /* !_CUBRID_GETOPT_H_ */
