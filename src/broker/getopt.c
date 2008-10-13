/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * getopt.c - 
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>

int optind = 0;
char *optarg;

int
getopt (int argc, char *argv[], char *fmt)
{
  static char *cur_p;
  char *p;

  if (optind == 0)
    {
      cur_p = argv[0];
      cur_p += strlen (cur_p);
    }

  if (*cur_p == '\0')
    {
      optind++;
      if (optind >= argc)
	{
	  return EOF;
	}
      cur_p = argv[optind];
      if (*cur_p == '-')
	{
	  cur_p++;
	}
      else
	{
	  return EOF;
	}
    }

  for (p = fmt; *p; p++)
    {
      if (*cur_p == *p)
	{
	  cur_p++;
	  if (*(p + 1) == ':')
	    {
	      if (*cur_p)
		{
		  optarg = cur_p;
		  cur_p += strlen (cur_p);
		}
	      else
		{
		  if (optind + 1 >= argc)
		    {
		      cur_p += strlen (cur_p);
		      return '?';
		    }
		  else
		    {
		      optind++;
		      optarg = argv[optind];
		      cur_p = optarg + strlen (optarg);
		    }
		}
	    }
	  return (*p);
	}
    }				/* end of for */

  cur_p += strlen (cur_p);
  return '?';
}
