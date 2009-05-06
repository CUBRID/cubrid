/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA 
 *
 */


/*
 * broker_getopt.c - 
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>

#include "broker_getopt.h"

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
