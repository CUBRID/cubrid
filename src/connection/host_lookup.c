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
#include <limits.h>
#include <errno.h>
#include <unordered_map>

#if defined (WINDOWS)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include "system_parameter.h"
#include "environment_variable.h"
#include "cci_common.h"

#define HOSTNAME_BUF_SIZE              (256)
#define MAX_HOSTS_LINE_NUM       (256)
#define IPADDR_LEN              (17)

#define FREE_MEM(PTR)           \
        do {                    \
          if (PTR) {            \
            free(PTR);          \
            PTR = 0;    \
          }                     \
        } while (0)

typedef enum
{
  HOSTNAME_TO_IPADDR = 0,
  IPADDR_TO_HOSTNAME = 1,
} LOOKUP_TYPE;

typedef enum
{
  INSERT_IPADDR = 0,
  INSERT_HOSTNAME = 1,
} HOSTENT_INSERT_TYPE;

typedef enum
{
  USE_GLIBC_HOSTS = 0,
  USE_USER_DEFINED_HOSTS = 1,
} HOST_LOOKUP_TYPE;

struct cub_hostent
{
  char ipaddr[IPADDR_LEN];
  char hostname[HOSTNAME_BUF_SIZE + 1];
};

typedef struct cub_hostent CUB_HOSTENT;
static CUB_HOSTENT hostent_List[MAX_HOSTS_LINE_NUM];	/* "etc/hosts" file maximum line */
static const char user_defined_hostfile_Name[] = "hosts.conf";

static int host_conf_element_Count = 0;

// *INDENT-OFF*
static std::unordered_map <std::string, std::string> user_host_Map;
// *INDENT-ON*

static struct hostent *hostent_init ();
static int host_conf_load ();
static struct hostent *host_lookup_internal (const char *hostname, struct sockaddr *saddr, LOOKUP_TYPE lookup_case);

/*
 * hostent_init () - Allocate memory hostent structure.
 * 
 * return   : the hostent pointer.
 *
 */
static struct hostent *
hostent_init ()
{
  struct hostent *hp;

  hp = NULL;

  if ((hp = (struct hostent *) malloc (sizeof (struct hostent))) == NULL)
    {
      return NULL;
    }

  hp->h_name = NULL;
  hp->h_aliases = NULL;
  hp->h_addr_list = NULL;

  if ((hp->h_name = (char *) malloc (sizeof (char) * (HOSTNAME_BUF_SIZE + 1))) == NULL
      || (hp->h_aliases = (char **) malloc (sizeof (char *))) == NULL
      || (hp->h_addr_list = (char **) malloc (sizeof (char *) * HOSTNAME_BUF_SIZE)) == NULL)
    {
      FREE_MEM (hp->h_addr_list);
      FREE_MEM (hp->h_aliases);
      FREE_MEM (hp->h_name);
      FREE_MEM (hp);
      return NULL;
    }

  hp->h_aliases[0] = NULL;
  hp->h_addr_list[0] = NULL;

  if ((hp->h_aliases[0] = (char *) malloc (sizeof (char) * (HOSTNAME_BUF_SIZE + 1))) == NULL
      || (hp->h_addr_list[0] = (char *) malloc (sizeof (char) * IPADDR_LEN)) == NULL)
    {
      FREE_MEM (hp->h_aliases[0]);
      FREE_MEM (hp->h_addr_list[0]);
      FREE_MEM (hp->h_addr_list);
      FREE_MEM (hp->h_aliases);
      FREE_MEM (hp->h_name);
      FREE_MEM (hp);

      return NULL;
    }

  hp->h_addrtype = AF_INET;
  hp->h_length = 4;

  return hp;
}

static struct hostent *
host_lookup_internal (const char *hostname, struct sockaddr *saddr, LOOKUP_TYPE lookup_case)
{
  static struct hostent *hp;
  int i, find_index = -1;

  char addr_transform_buf[IPADDR_LEN];
  char addr_transform_buf2[IPADDR_LEN];
  struct sockaddr_in *addr_trans = NULL;

  if (host_conf_element_Count == 0)
    {
      host_conf_element_Count = host_conf_load ();
      if (host_conf_element_Count <= 0)
	{
//err_set : load fail
	  return NULL;
	}
    }

  addr_trans = (struct sockaddr_in *) saddr;

  assert (((hostname != NULL) && (saddr == NULL)) || ((hostname == NULL) && (saddr != NULL)));

  if (lookup_case == IPADDR_TO_HOSTNAME)
    {
      if (inet_ntop (AF_INET, &addr_trans->sin_addr, addr_transform_buf, sizeof (addr_transform_buf)) == NULL)
	{
//err_msg : convert binary to text fail
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
//err_msg : hosts.conf has no info
    }

  if (inet_pton (AF_INET, hostent_List[find_index].ipaddr, addr_transform_buf2) < 1)
    {
//err_set : convert binary to text fail -> reverse
      return NULL;
    }

  /*set the hostent struct for the return value */
  if ((hp = hostent_init ()) == NULL)
    {
      return NULL;
    }

  strcpy (hp->h_name, hostent_List[find_index].hostname);
  strcpy (hp->h_aliases[0], hostent_List[find_index].hostname);
  memcpy (hp->h_addr_list[0], addr_transform_buf2, sizeof (hp->h_addr_list[0]));

  return hp;
}

static int
host_conf_load ()
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
  bool hostent_flag;
  HOSTENT_INSERT_TYPE hostent_insert_Type;

  memset (file_line, 0, HOSTNAME_BUF_SIZE + 1);
  memset (line_buf, 0, HOSTNAME_BUF_SIZE + 1);

  hosts_conf_dir = envvar_confdir_file (host_conf_file_full_path, PATH_MAX, "hosts.conf");
  fp = fopen (hosts_conf_dir, "r");
  if (fp == NULL)
    {
//err_msg : file open fail
//er_set_with_oserror (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, host_conf_file_full_path);
//fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR, -ER_IO_MOUNT_FAIL));
      return -1;
    }

  while (fgets (file_line, HOSTNAME_BUF_SIZE + 1, fp) != NULL)
    {
      if (file_line[0] == '#')
	continue;
      if (file_line[0] == '\n')
	continue;

      token = strtok_r (file_line, delim, &save_ptr_strtok);
      hostent_flag = INSERT_HOSTNAME;

      char map_ipaddr[IPADDR_LEN];
      char map_hostname[HOSTNAME_BUF_SIZE + 1];

      do
	{
	  if (*token == '\0' || *token == '#')
	    {
	      break;
	    }
	  if (hostent_flag == INSERT_HOSTNAME)
	    {
	      if (strlen (token) > HOSTNAME_BUF_SIZE + 1)
		{
//err_msg 
//er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ?, 1, token);
//ex) Hostname "%1$s" is too long, it should be less than or equal 256
		  return -1;
		}
	      strcpy (hostent_List[host_count].ipaddr, token);
	      strcpy (map_ipaddr, token);

	      hostent_flag = INSERT_IPADDR;
	    }
	  else
	    {
	      if (strlen (token) > IPADDR_LEN)
		{
//err_msg 
//er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ?2, 1, token);
//ex) IP address "%1$s" is too long, it should be less than or equal 256
		  return -1;
		}

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
//err_msg : duplicated 
//er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ?2, 1, token);
//ex) Hostname "%1$s" already exists, a hostname cannot be duplicated with other IP addresses
	      return -1;
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

  if (prm_get_bool_value (PRM_ID_USE_USER_HOSTS) == USE_GLIBC_HOSTS)
    {
      hp = gethostbyname (name);
    }
  else
    {
      hp = host_lookup_internal (name, NULL, HOSTNAME_TO_IPADDR);
    }

  return hp;
}

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
#else /* WINDOWS */
int
gethostbyname_r_uhost (const char *name,
		       struct hostent *ret, char *buf, size_t buflen, struct hostent **result, int *h_errnop)
#endif				/* HAVE_GETHOSTBYNAME_R */
{
#if defined (WINDOWS)
  return 0;
#endif

  struct hostent *hp_buf = NULL;

  if (prm_get_bool_value (PRM_ID_USE_USER_HOSTS) == USE_GLIBC_HOSTS)
    {
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
#endif
    }
  else
    {
      hp_buf = host_lookup_internal (name, NULL, HOSTNAME_TO_IPADDR);
    }

#ifdef HAVE_GETHOSTBYNAME_R
#if defined (HAVE_GETHOSTBYNAME_R_GLIBC)
  if (hp_buf == NULL)
    {
//err_print fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR, -ER_FAILED));
      return EFAULT;
    }

  if (((*result) = (struct hostent *) malloc (sizeof (struct hostent))) == NULL)
    {
      return NULL;
    }
  memcpy ((void *) ret, (void *) hp_buf, sizeof (struct hostent));
  memcpy ((void *) *result, (void *) hp_buf, sizeof (struct hostent));

  return 0;
#elif defined (HAVE_GETHOSTBYNAME_R_SOLARIS)
  if (hp_buf == NULL)
    {
//err_print fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR, -ER_FAILED));
      return NULL;
    }

  memcpy ((void *) ret, (void *) hp_buf, sizeof (struct hostent));

  return ret;
#elif defined (HAVE_GETHOSTBYNAME_R_HOSTENT_DATA)
  if (hp_buf == NULL)
    {
//err_print fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR, -ER_FAILED));
      return -1;
    }

  memcpy ((void *) ret, (void *) hp_buf, sizeof (struct hostent));

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
  struct hostent *hp = NULL;

  if (prm_get_bool_value (PRM_ID_USE_USER_HOSTS) == USE_GLIBC_HOSTS)
    {
      return getnameinfo (addr, addrlen, host, hostlen, NULL, 0, NI_NOFQDN);
    }
  else
    {
      if ((hp = host_lookup_internal (NULL, addr, IPADDR_TO_HOSTNAME)) != NULL)
	{
	  strncpy (host, hp->h_name, hostlen);
	  host[CUB_MAXHOSTNAMELEN - 1] = '\0';

	  return 0;
	}

      return EINVAL;
    }

}

int
getaddrinfo_uhost (char *node, char *service, struct addrinfo *hints, struct addrinfo **res)
{
  struct hostent *hp = NULL;
  struct addrinfo results_out;
  struct sockaddr_in addr_convert;
  struct in_addr *in_addr_buf = NULL;

  if (prm_get_bool_value (PRM_ID_USE_USER_HOSTS) == USE_GLIBC_HOSTS)
    {
      return getaddrinfo (node, service, hints, res);
    }
  else
    {
      hp = host_lookup_internal (node, NULL, HOSTNAME_TO_IPADDR);
    }

  if (hp == NULL)
    {
//err_print fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR, -ER_FAILED));
      return EAI_NODATA;
    }

  if ((in_addr_buf = (struct in_addr *) malloc (sizeof (struct in_addr))) == NULL)
    {
      free (hp);
//er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (struct in_addr));
      return EAI_MEMORY;
    }

  /*Constitute struct addrinfo for the out parameter res */
  if (((*res) = (struct addrinfo *) malloc (sizeof (struct addrinfo))) == NULL)
    {
      free (hp);
      free (in_addr_buf);
//er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (struct in_addr));
      return EAI_MEMORY;
    }

  memset (&results_out, 0, sizeof (results_out));
  if ((results_out.ai_addr = (struct sockaddr *) malloc (sizeof (struct sockaddr))) == NULL)
    {
      free (hp);
      free (in_addr_buf);
      freeaddrinfo (*res);
      freeaddrinfo (&results_out);
//er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (struct in_addr));
      return EAI_MEMORY;

    }
  results_out.ai_flags = hints->ai_flags;

  results_out.ai_family = hints->ai_family;

  results_out.ai_socktype = hints->ai_socktype;

  results_out.ai_protocol = IPPROTO_TCP;

  memcpy (&in_addr_buf->s_addr, hp->h_addr_list[0], sizeof (in_addr_buf->s_addr));
  memcpy (&addr_convert.sin_addr, &in_addr_buf->s_addr, sizeof (addr_convert.sin_addr));
  memcpy (results_out.ai_addr, (struct sockaddr *) &addr_convert, sizeof (struct sockaddr));

  results_out.ai_addrlen = sizeof (results_out.ai_addr);

  results_out.ai_canonname = hp->h_name;

  results_out.ai_next = NULL;

  memmove (*res, &results_out, sizeof (struct addrinfo));

  FREE_MEM (in_addr_buf);
  FREE_MEM (results_out.ai_addr);

  return 0;
}
