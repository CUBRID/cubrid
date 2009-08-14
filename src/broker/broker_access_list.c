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
 * broker_access_list.c -
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "broker_access_list.h"
#include "broker_error.h"
#include "broker_util.h"

static int ip_comp (const void *arg1, const void *arg2);
static int convert_ip (char *str, T_IP * ip_addr);
static int ipstr2int (char *str, char **endp, char next_char);

T_ACL *v3_acl = NULL;

int
uw_acl_make (char *acl_file)
{
  FILE *fp;
  char read_buf[1024];
  char *p;
  T_IP ip_addr;
  T_IP *acl = NULL;
  int num_acl = 0;

  v3_acl = NULL;

  if (acl_file[0] == '\0')
    {
      return 0;
    }

  v3_acl = (T_ACL *) malloc (sizeof (T_ACL));
  if (v3_acl == NULL)
    {
      UW_SET_ERROR_CODE (UW_ER_NO_MORE_MEMORY, 0);
      return -1;
    }
  v3_acl->num_acl = 0;
  v3_acl->acl = NULL;

  fp = fopen (acl_file, "r");
  if (fp == NULL)
    return 0;

  while (fgets (read_buf, sizeof (read_buf), fp) != NULL)
    {
      p = trim (read_buf);
      if (p[0] == '#')
	continue;
      if (convert_ip (p, &ip_addr) < 0)
	continue;
      num_acl++;
      if (acl == NULL)
	acl = (T_IP *) malloc (sizeof (T_IP));
      else
	acl = (T_IP *) realloc (acl, sizeof (T_IP) * num_acl);
      if (acl == NULL)
	{
	  fclose (fp);
	  UW_SET_ERROR_CODE (UW_ER_NO_MORE_MEMORY, 0);
	  return -1;
	}
      acl[num_acl - 1] = ip_addr;
    }

  if (acl != NULL)
    {
      qsort (acl, num_acl, sizeof (T_IP), ip_comp);
    }
  v3_acl->num_acl = num_acl;
  v3_acl->acl = acl;

  fclose (fp);

  return 0;
}

int
uw_acl_check (unsigned char *ip_addr)
{
  int i;
  size_t len;

  for (i = 0; i < v3_acl->num_acl; i++)
    {
      if (v3_acl->acl[i].ip_length > IPV4_LENGTH_MAX)
        {
          len = IPV4_LENGTH_MAX;
        }
      else
        {
          len = v3_acl->acl[i].ip_length;
        }

      if (memcmp (ip_addr, v3_acl->acl[i].ip, len) == 0)
        {
          return 0;
        }
    }

  return -1;
}

static int
ip_comp (const void *arg1, const void *arg2)
{
  const T_IP *ip1 = (const T_IP *) arg1;
  const T_IP *ip2 = (const T_IP *) arg2;

  return ((ip1->ip_length) - (ip2->ip_length));
}

static int
convert_ip (char *str, T_IP * ip_addr)
{
  int i;
  char *endp;
  int val;
  char end_char;

  memset (ip_addr, 0, sizeof (T_IP));

  for (i = 0; i < 4; i++)
    {
      end_char = '.';
      if (i == 3)
	end_char = '\0';

      val = ipstr2int (str, &endp, end_char);
      if (val < 0)
	{
	  return -1;
	}
      if (val == 256)
	{
	  ip_addr->ip_length = i;
	  return 0;
	}
      else
	ip_addr->ip[i] = (unsigned char) val;

      str = endp + 1;
    }
  ip_addr->ip_length = 4;
  return 0;
}

static int
ipstr2int (char *str, char **endp, char next_char)
{
  int val;

  if (*str == '*')
    {
      if (*(str + 1) != '\0')
	{
	  return -1;
	}
      return 256;
    }
  val = strtol (str, endp, 10);
  if ((*endp == str) || (**endp != next_char))
    {
      return -1;
    }
  if (val < 0 || val > 255)
    return -1;

  return val;
}
