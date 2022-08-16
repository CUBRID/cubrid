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

#include <unordered_map>

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#if defined (WINDOWS)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include "system_parameter.h"
#include "environment_variable.h"

#define HOSTNAME_BUF_SIZE              (256)
#define MAX_HOSTS_LINE_NUM       (256)
#define IPADDR_LEN              (17)

#define IPADDR_STORE 0x0
#define HOSTNAME_STORE 0x1

typedef enum
{
  HOSTNAME_TO_IPADDR = 0,
  IPADDR_TO_HOSTNAME = 1,
} LOOKUP_TYPE;

struct cub_hostent
{
  char ipaddr[IPADDR_LEN];
  char hostname[HOSTNAME_BUF_SIZE + 1];
};

typedef struct cub_hostent CUB_HOSTENT;
static CUB_HOSTENT hostent_List[MAX_HOSTS_LINE_NUM];	/* "etc/hosts" file maximum line */
//should I need to allocate all 256 element??
static const char user_defined_hostfile_Name[] = "hosts.conf";

static int host_conf_element_Count = 0;
static int host_conf_Use = -1;	/* -1 is unknown, 0 is not using user hosts, 1 is using user hosts */

// *INDENT-OFF*
static std::unordered_map <std::string, std::string> user_host_Map;
// *INDENT-ON*

static int host_conf_load ();
static struct hostent *host_lookup_internal (const char *hostname, struct sockaddr *saddr, LOOKUP_TYPE lookup_case);

static struct hostent *
host_lookup_internal (const char *hostname, struct sockaddr *saddr, LOOKUP_TYPE lookup_case)
{
  static struct hostent hp;
  int i, find_index = -1;

  char addr_transform_buf[IPADDR_LEN];
  char addr_transform_buf2[IPADDR_LEN];
  struct sockaddr_in *addr_trans = NULL;

  if (host_conf_element_Count == 0)
    {
      host_conf_element_Count = host_conf_load ();
      if (host_conf_element_Count <= 0)
	{
	  return NULL;
	}
    }

  addr_trans = (struct sockaddr_in *) saddr;

  assert (((hostname != NULL) && (saddr == NULL)) || ((hostname == NULL) && (saddr != NULL)));

  if (lookup_case == IPADDR_TO_HOSTNAME)
    {
      if (inet_ntop (AF_INET, &addr_trans->sin_addr, addr_transform_buf, sizeof (addr_transform_buf) + 1) == NULL)
	{
	  return NULL;
	}

    }

/*Look up in the hostent_List*/
  for (i = 0; i < host_conf_element_Count; i++)
    {
      if ((lookup_case == HOSTNAME_TO_IPADDR) && (strcmp (hostname, hostent_List[i].hostname) == 0))
	{
	  find_index = i;
	  break;
	}

      if ((lookup_case == IPADDR_TO_HOSTNAME) && (strcmp (addr_transform_buf, hostent_List[i].ipaddr) == 0))
	{
	  find_index = i;
	  break;
	}
    }

  if (find_index < 0)
    {
      return NULL;
//this should be set to error message
    }

  if (inet_pton (AF_INET, hostent_List[find_index].ipaddr, addr_transform_buf2) < 1)
    {
      return NULL;
    }

  /*set the hostent struct for the return value */
  hp.h_addrtype = AF_INET;
  hp.h_length = 4;
  hp.h_name = (char *) malloc (sizeof (char) * HOSTNAME_BUF_SIZE);
  hp.h_aliases = (char **) malloc (sizeof (char *));
  hp.h_aliases[0] = (char *) malloc (sizeof (char) * HOSTNAME_BUF_SIZE);
  hp.h_addr_list = (char **) malloc (sizeof (char *) * HOSTNAME_BUF_SIZE);
  hp.h_addr_list[0] = (char *) malloc (sizeof (char) * IPADDR_LEN);

  strcpy (hp.h_name, hostent_List[find_index].hostname);
  strcpy (hp.h_aliases[0], hostent_List[find_index].hostname);
  memcpy (hp.h_addr_list[0], addr_transform_buf2, sizeof (hp.h_addr_list[0]));

  return &hp;
}

static int
host_conf_load ()		//set hash table to discover error
{
  FILE *fp;
  char file_line[HOSTNAME_BUF_SIZE + 1];
  char line_buf[HOSTNAME_BUF_SIZE + 1];
  char host_conf_file_full_path[PATH_MAX];
  char *hosts_conf_dir;

  char *token;
  char *save_ptr_strtok;
  /*delimiter */
  char *delim = " \t\n";

  int host_count = 0;

  /*False, if hostent_List[index].hostname is set */
  bool storage_flag = IPADDR_STORE;

  memset (file_line, 0, HOSTNAME_BUF_SIZE + 1);
  memset (line_buf, 0, HOSTNAME_BUF_SIZE + 1);

  host_count = host_conf_element_Count;
  assert (!host_count);

  hosts_conf_dir = envvar_confdir_file (host_conf_file_full_path, PATH_MAX, "hosts.conf");
  fp = fopen (hosts_conf_dir, "r");
  if (fp == NULL)
    {
      return -1;
      //error msg-> temp
    }

  while (fgets (file_line, HOSTNAME_BUF_SIZE, fp) != NULL)
    {
//verifying duplicated hostname will be implemented soon
      if (file_line[0] == '#')
	continue;
      if (file_line[0] == '\n')
	continue;

      token = strtok_r (file_line, delim, &save_ptr_strtok);
      storage_flag = IPADDR_STORE;

      char map_ipaddr[HOSTNAME_BUF_SIZE];
      char map_hostname[HOSTNAME_BUF_SIZE];

      do
	{
	  if (*token == '\0' || *token == '#')
	    {
	      break;
	    }
	  if (!storage_flag)
	    {
	      strcpy (hostent_List[host_count].ipaddr, token);
	      strcpy (map_ipaddr, token);

	      storage_flag = HOSTNAME_STORE;
	    }
	  else
	    {
	      strcpy (hostent_List[host_count].hostname, token);
	      strcpy (map_hostname, token);
	    }
	}
      while (token = strtok_r (NULL, delim, &save_ptr_strtok));
//naming rule check, detailed rule, error set
//if hostent_List[host_count].hostname is empty, set error
      if (hostent_List[host_count].hostname != NULL)
	{
	  if ((user_host_Map.find (map_hostname) == user_host_Map.end ()))
	    {
	      user_host_Map[map_hostname] = map_ipaddr;
	    }
/*duplicated hostname*/
	  else if (!strcmp (user_host_Map.find (map_hostname)->second.c_str (), hostent_List[host_count].ipaddr))
	    {
/*duplicated hostname and ip address both*/
	    }
	  else
	    {
/*duplicated hostname but different ip address*/
	      return NULL;
	    }
	  host_count++;
	}
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
struct hostent *
gethostbyname_uhost (char *name)
{
  struct hostent *hp;

  if (host_conf_Use < 0)
    {
      host_conf_Use = prm_get_bool_value (PRM_ID_USE_USER_HOSTS);
    }

  if (host_conf_Use == 0)
    {
      return gethostbyname (name);
    }

  hp = host_lookup_internal (name, NULL, HOSTNAME_TO_IPADDR);

  if (hp == NULL)
    {
      return NULL;
    }

  return hp;
}

//gethostbyname_r_uhost (const char *hostname, struct hostent *out_hp)
#ifdef HAVE_GETHOSTBYNAME_R
#if defined (HAVE_GETHOSTBYNAME_R_GLIBC)
int
gethostbyname_r_uhost (const char *name,
		       struct hostent *ret, char *buf, size_t buflen, struct hostent **result, int *h_errnop)
#elif defined (HAVE_GETHOSTBYNAME_R_SOLARIS)
struct hostent *
gethostbyname_r_uhost (const char *name, struct hostent *ret, char *buf, size_t buflen, int *h_errnop)
#elif defined (HAVE_GETHOSTBYNAME_R_HOSTENT_DATA)
int
gethostbyname_r_uhost (const char *name, struct hostent *ret, struct hostent_data *ht_data)
#else
#error "HAVE_GETHOSTBYNAME_R"
#endif /* HAVE_GETHOSTBYNAME_R_GLIBC */
#endif /* HAVE_GETHOSTBYNAME_R */
{
  struct hostent *hp_memcpy = NULL;

  if (host_conf_Use < 0)
    {
      host_conf_Use = prm_get_bool_value (PRM_ID_USE_USER_HOSTS);
    }

  if (host_conf_Use == 0)
    {
//err also modified by callee
#ifdef HAVE_GETHOSTBYNAME_R
#if defined (HAVE_GETHOSTBYNAME_R_GLIBC)
      return gethostbyname_r (name, ret, buf, buflen, result, h_errnop);
#elif defined (HAVE_GETHOSTBYNAME_R_SOLARIS)
      return gethostbyname_r (name, ret, buf, buflen, h_errnop);
#elif defined (HAVE_GETHOSTBYNAME_R_HOSTENT_DATA)
      return gethostbyname_r (name, ret, &ht_data);
#else
#error "HAVE_GETHOSTBYNAME_R"
#endif
#endif /* !HAVE_GETHOSTBYNAME_R */
    }

  hp_memcpy = host_lookup_internal (name, NULL, HOSTNAME_TO_IPADDR);
  memcpy ((void *) ret, (void *) hp_memcpy, sizeof (struct hostent));

#ifdef HAVE_GETHOSTBYNAME_R
#if defined (HAVE_GETHOSTBYNAME_R_GLIBC)
  if (ret == NULL)
    {
      return EFAULT;
    }
  (*result) = (struct hostent *) malloc (sizeof (struct hostent));
  memcpy ((void *) *result, (void *) hp_memcpy, sizeof (struct hostent));
  return 0;
#elif defined (HAVE_GETHOSTBYNAME_R_SOLARIS)
  if (ret == NULL)
    {
      return EFAULT;
    }

  return ret;
#elif defined (HAVE_GETHOSTBYNAME_R_HOSTENT_DATA)
  if (ret == NULL)
    {
      return EFAULT;
    }

  return 0;
#else
#error "HAVE_GETHOSTBYNAME_R"
#endif /* HAVE_GETHOSTBYNAME_R_GLIBC */
#endif /* HAVE_GETHOSTBYNAME_R */
}

int
getnameinfo_uhost (struct sockaddr *addr, socklen_t addrlen, char *host, size_t hostlen, char *serv, size_t servlen,
		   int flags)
{
  struct hostent *hp;

  if (host_conf_Use < 0)
    {
      host_conf_Use = prm_get_bool_value (PRM_ID_USE_USER_HOSTS);
    }

  if (host_conf_Use == 0)
    {
      return getnameinfo (addr, addrlen, host, hostlen, NULL, 0, NI_NOFQDN);
    }

  hp = host_lookup_internal (NULL, addr, IPADDR_TO_HOSTNAME);

  if (hp == NULL)
    {
      return EAI_NODATA;
    }

  strcpy (host, hp->h_name);
  return 0;
}

int
getaddrinfo_uhost (char *node, char *service, struct addrinfo *hints, struct addrinfo **res)
{

  struct hostent *hp = NULL;
  struct addrinfo results_out;
  struct sockaddr_in addr_convert;
  struct in_addr *in_addr_buf;

  if (host_conf_Use < 0)
    {
      host_conf_Use = prm_get_bool_value (PRM_ID_USE_USER_HOSTS);
    }

  if (host_conf_Use == 0)
    {
      return getaddrinfo (node, NULL, hints, res);
    }

  memset (&results_out, 0, sizeof (results_out));

  in_addr_buf = (struct in_addr *) malloc (sizeof (struct in_addr));

  hp = host_lookup_internal (node, NULL, HOSTNAME_TO_IPADDR);

  if (hp == NULL)
    {
      free (in_addr_buf);
      return EAI_NODATA;
    }

  /*Constitute struct addrinfo for the out parameter res */
  *res = (struct addrinfo *) malloc (sizeof (struct addrinfo));
  results_out.ai_flags = hints->ai_flags;

  results_out.ai_family = hints->ai_family;

  results_out.ai_socktype = hints->ai_socktype;

  results_out.ai_protocol = IPPROTO_TCP;

  memcpy (&in_addr_buf->s_addr, hp->h_addr_list[0], sizeof (in_addr_buf->s_addr));
  memcpy (&addr_convert.sin_addr, &in_addr_buf->s_addr, sizeof (addr_convert.sin_addr));
  results_out.ai_addr = (struct sockaddr *) malloc (sizeof (struct sockaddr));
  memcpy (results_out.ai_addr, (struct sockaddr *) &addr_convert, sizeof (struct sockaddr));

  results_out.ai_addrlen = sizeof (results_out.ai_addr);

  results_out.ai_canonname = hp->h_name;

  results_out.ai_next = NULL;

  memmove (*res, &results_out, sizeof (struct addrinfo));

  free (in_addr_buf);

  return 0;
}
