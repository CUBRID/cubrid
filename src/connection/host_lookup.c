/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * host_lookup.c
 */

#ident "$Id$"

#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <limits.h>
#include <arpa/inet.h>

#include "system_parameter.h"
#include "environment_variable.h"

#ifndef HAVE_GETHOSTBYNAME_R
#include <pthread.h>
static pthread_mutex_t gethostbyname_lock = PTHREAD_MUTEX_INITIALIZER;
#endif /* HAVE_GETHOSTBYNAME_R */


#define HOSTNAME_BUF_SIZE              (256)

struct cub_hostent
{
  char addr[17];
  char hostname[HOSTNAME_BUF_SIZE + 1];
};

typedef struct cub_hostent CUB_HOSTENT;
static CUB_HOSTENT hostent_List[256];	/* "etc/hosts" file maximum line */
//should I need to allocate all 256 element??
static const char user_defined_hostfile_Name[] = "hosts.conf";

static int host_conf_element_Count = 0;
static int host_conf_Use = -1;

static int host_conf_load ();
static hostent *hostent_compose (const char *hostname);

static hostent *
hostent_compose (const char *hostname)
{
  static hostent hp;
  int hostent_list_index, find_index, ret;
  char addr_transform_buf[17];

  for (hostent_list_index = 0; hostent_list_index < host_conf_element_Count; hostent_list_index++)
    {
      if (!strcmp (hostname, hostent_List[hostent_list_index].hostname))
	{
	  find_index = hostent_list_index;
	  break;
	}
    }
  if (hostent_list_index == host_conf_element_Count)
    {
      return NULL;
//this should be set to error message
    }
  if (inet_pton (AF_INET, hostent_List[find_index].addr, addr_transform_buf) < 1)
    {
      return NULL;
    }

  hp.h_addrtype = AF_INET;
  hp.h_length = 4;
  hp.h_name = (char *) malloc (sizeof (char *));
  hp.h_aliases = (char **) malloc (sizeof (char **));
  hp.h_aliases[0] = (char *) malloc (sizeof (char *));
  hp.h_addr_list = (char **) malloc (sizeof (char **));
  hp.h_addr_list[0] = (char *) malloc (sizeof (char *));

  strcpy (hp.h_name, hostname);
  strcpy (hp.h_aliases[0], hostname);
  strcpy (hp.h_addr_list[0], addr_transform_buf);

  return &hp;
}

static int
host_conf_load ()		//set hash table to discover error
{
  FILE *fp;
  char file_line[HOSTNAME_BUF_SIZE + 1];
  char line_buf[HOSTNAME_BUF_SIZE + 1];
  char host_conf_file_full_path[PATH_MAX];
  char *buf;
  char *token;
  char *save_ptr;
  char *delim = " \t\n";	/*delimiter */
  int host_count = 0, last = 0, line_no = 0;
  bool flag = 0;		//it should be changed -> temp

  memset (file_line, 0, HOSTNAME_BUF_SIZE + 1);
  memset (line_buf, 0, HOSTNAME_BUF_SIZE + 1);

  host_count = host_conf_element_Count;
  assert (!host_count);

  buf = envvar_confdir_file (host_conf_file_full_path, PATH_MAX, "hosts.conf");
  fp = fopen (buf, "r");
  if (fp == NULL)
    {
      return -1;
      //error msg-> temp
    }

  while (fgets (file_line + last, HOSTNAME_BUF_SIZE - last, fp) != NULL)
    {
//verifying duplicated hostname will be implemented soon
      if (file_line[0] == '#')
	continue;
      if (file_line[0] == ' ')
	continue;
      if (file_line[0] == '\n')
	continue;

      token = strtok_r (file_line, delim, &save_ptr);
      flag = false;

      do
	{
	  if (*token == '\0' || *token == '#')
	    {
	      break;
	    }
	  if (!flag)
	    {
	      strcpy (hostent_List[host_count].addr, token);
	      flag = true;
	    }
	  else
	    {
	      strcpy (hostent_List[host_count].hostname, token);
	    }
	}
      while (token = strtok_r (NULL, delim, &save_ptr));
      host_count++;
    }
  return host_count;
}

/*
 * gethostbyname_uhost () - Do same job with gethostbyname (), using by the 'user' defined 'hosts.conf' file.
 * 
 * return   : the hostent pointer.
 * hostname (in) : the hostname.
 *
 */
hostent *
gethostbyname_uhost (char *hostname)
{
  hostent *hp;
  bool host_conf;
  int index;

  if (host_conf_Use < 0)
    {
      host_conf_Use = prm_get_bool_value (PRM_ID_USE_USER_HOSTS);
    }

  if (!host_conf_Use)
    {
      return gethostbyname (hostname);
    }

  if (!host_conf_element_Count)
    {
      host_conf_element_Count = host_conf_load ();
      if (host_conf_element_Count <= 0)
	{
	  return NULL;
	}
    }

  hp = hostent_compose (hostname);
  //hp null case error 
  return hp;
}

int
gethostbyname_r_uhost (const char *hostname, struct hostent *out_hp)
{
  struct hostent *hp_memcpy;
  bool host_conf;
  int ret;

  if (host_conf_Use < 0)
    {
      host_conf_Use = prm_get_bool_value (PRM_ID_USE_USER_HOSTS);
    }

  if (!host_conf_Use)
    {
//err also modified by callee
#ifdef HAVE_GETHOSTBYNAME_R
#if defined (HAVE_GETHOSTBYNAME_R_GLIBC)
      struct hostent *hp, hent;
      int herr;
      char buf[1024];

      ret = gethostbyname_r (hostname, &hent, buf, sizeof (buf), &hp, &herr);
#elif defined (HAVE_GETHOSTBYNAME_R_SOLARIS)
      struct hostent hent;
      int herr;
      char buf[1024];

      ret = gethostbyname_r (hostname, &hent, buf, sizeof (buf), &herr);
#elif defined (HAVE_GETHOSTBYNAME_R_HOSTENT_DATA)
      struct hostent hent;
      struct hostent_data ht_data;

      ret = gethostbyname_r (hostname, &hent, &ht_data);
#else
#error "HAVE_GETHOSTBYNAME_R"
#endif
#else /* HAVE_GETHOSTBYNAME_R */
      struct hostent *hent;
      int r;

      r = pthread_mutex_lock (&gethostbyname_lock);
      hent = gethostbyname (hostname);
      if (hent == NULL)
#endif /* !HAVE_GETHOSTBYNAME_R */
	memcpy (out_hp, &hent, sizeof (hent));
      out_hp = &hent;
      return ret;
    }

  if (!host_conf_element_Count)
    {
      host_conf_element_Count = host_conf_load ();
      if (host_conf_element_Count <= 0)
	{
	  return NULL;
	}
    }

  hp_memcpy = hostent_compose (hostname);
  memcpy (out_hp, hp_memcpy, sizeof (*hp_memcpy));
  //out_hp NULL case
  return 0;
}
