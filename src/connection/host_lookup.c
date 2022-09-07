/*
 *
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
#include "message_catalog.h"


#define HOSTNAME_BUF_SIZE              (128)
#define MAX_HOSTS_LINE_SIZE       (256)
#define IPADDR_LEN              (17)

#define FREE_MEM(PTR)           \
        do {                    \
          if (PTR) {            \
            free(PTR);          \
            PTR = 0;    \
          }                     \
        } while (0)

#define FREE_HOSTENT_MEM(PTR)        \
        do {                    \
          if (PTR) {            \
            FREE_MEM (PTR->h_aliases[0]);       \
            FREE_MEM (PTR->h_addr_list[0]);     \
            FREE_MEM (PTR->h_addr_list);                \
            FREE_MEM (PTR->h_aliases);          \
            FREE_MEM (PTR->h_name);             \
            FREE_MEM (PTR);                     \
           }                                    \
        }while(0)

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

typedef enum
{
  LOAD_FAIL = -1,
  LOAD_INIT,
  LOAD_SUCCESS,
} HOSTS_CONF_LOAD_STATUS;

struct cub_hostent
{
  char ipaddr[IPADDR_LEN];
  char hostname[HOSTNAME_BUF_SIZE + 1];
};

typedef struct cub_hostent CUB_HOSTENT;
static const char user_defined_hostfile_Name[] = "hosts.conf";

static int hosts_conf_file_Load = LOAD_INIT;

/*Hostname and IP address are both stored in the user_host_Map to search both of them*/
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
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (struct hostent));
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

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (char) * (HOSTNAME_BUF_SIZE + 1));
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

      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (char) * (HOSTNAME_BUF_SIZE + 1));
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

  char addr_trans_ch_buf[IPADDR_LEN];
  char addr_trans_bi_buf[IPADDR_LEN];
  char hostname_buf[HOSTNAME_BUF_SIZE + 1];
  char ipaddr_buf[IPADDR_LEN];
  struct sockaddr_in *addr_trans = NULL;

  if (hosts_conf_file_Load == LOAD_INIT)
    {
      if ((hosts_conf_file_Load = host_conf_load ()) == LOAD_FAIL)
	{
//err_set : load fail
	  return NULL;
	}
    }

  addr_trans = (struct sockaddr_in *) saddr;

  assert (((hostname != NULL) && (saddr == NULL)) || ((hostname == NULL) && (saddr != NULL)));

  if (lookup_case == IPADDR_TO_HOSTNAME)
    {
      if (inet_ntop (AF_INET, &addr_trans->sin_addr, addr_trans_ch_buf, sizeof (addr_trans_ch_buf)) == NULL)
	{
	  fprintf (stderr, "Convertion IP address from binary form to text is failed");
	  return NULL;
	}

    }

  /*Look up in the user_host_Map */
  if ((lookup_case == HOSTNAME_TO_IPADDR) && (user_host_Map.find (hostname) != user_host_Map.end ()))
    {
      strcpy (ipaddr_buf, user_host_Map.find (hostname)->second.c_str ());
      strcpy (hostname_buf, hostname);
    }
  else if ((lookup_case == IPADDR_TO_HOSTNAME) && (user_host_Map.find (addr_trans_ch_buf) != user_host_Map.end ()))
    {
      strcpy (hostname_buf, user_host_Map.find (addr_trans_ch_buf)->second.c_str ());
      strcpy (ipaddr_buf, addr_trans_ch_buf);
    }
  else
    {
      if (lookup_case == HOSTNAME_TO_IPADDR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UHOST_CANT_LOOKUP_INFO, 1, hostname);
	  fprintf (stdout, "%s\n", er_msg ());
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UHOST_CANT_LOOKUP_INFO, 1, addr_trans_ch_buf);
	  fprintf (stdout, "%s\n", er_msg ());
	}
      return NULL;
    }

  if (inet_pton (AF_INET, ipaddr_buf, addr_trans_bi_buf) < 1)
    {
      fprintf (stderr, "Convertion IP address from text form to binary is failed");
      return NULL;
    }

  /*set the hostent struct for the return value */
  if ((hp = hostent_init ()) == NULL)
    {
      return NULL;
    }

  strcpy (hp->h_name, hostname_buf);
  strcpy (hp->h_aliases[0], hostname_buf);
  memcpy (hp->h_addr_list[0], addr_trans_bi_buf, sizeof (hp->h_addr_list[0]));

  return hp;
}

static int
host_conf_load ()
{
  FILE *fp;
  char file_line[MAX_HOSTS_LINE_SIZE + 1];
  char host_conf_file_full_path[PATH_MAX];
  char *hosts_conf_dir;

  char *token;
  char *save_ptr_strtok;
  /*delimiter */
  char *delim = " \t\n";
  char map_ipaddr[IPADDR_LEN];
  char map_hostname[HOSTNAME_BUF_SIZE + 1];

  /*True, when the string token has hostname, otherwise, string token has IP address */
  bool hostent_flag;
  HOSTENT_INSERT_TYPE hostent_insert_Type;

  memset (file_line, 0, MAX_HOSTS_LINE_SIZE + 1);

  hosts_conf_dir = envvar_confdir_file (host_conf_file_full_path, PATH_MAX, "hosts.conf");
  fp = fopen (hosts_conf_dir, "r");
  if (fp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, host_conf_file_full_path);
      fprintf (stdout, "%s\n", er_msg ());
      return LOAD_FAIL;
    }

  while (fgets (file_line, MAX_HOSTS_LINE_SIZE + 1, fp) != NULL)
    {
      if (file_line[0] == '#')
	continue;
      if (file_line[0] == '\n')
	continue;

      token = strtok_r (file_line, delim, &save_ptr_strtok);
      hostent_flag = INSERT_IPADDR;

      do
	{
	  if (*token == NULL || *token == '#')
	    {
	      break;
	    }
	  if (hostent_flag == INSERT_IPADDR)
	    {
	      if (strlen (token) > IPADDR_LEN)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UHOST_IP_ADDR_TOO_LONG, 1, token);
		  fprintf (stdout, "%s\n", er_msg ());

		  user_host_Map.clear ();

		  fclose (fp);

		  return LOAD_FAIL;
		}
	      strcpy (map_ipaddr, token);

	      hostent_flag = INSERT_HOSTNAME;
	    }
	  else
	    {
	      if (strlen (token) > HOSTNAME_BUF_SIZE + 1)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UHOST_HOST_NAME_TOO_LONG, 1, token);
		  fprintf (stdout, "%s\n", er_msg ());

		  user_host_Map.clear ();

		  fclose (fp);

		  return LOAD_FAIL;
		}
	      strcpy (map_hostname, token);
	    }
	}
      while (token = strtok_r (NULL, delim, &save_ptr_strtok));

      if (strcmp ("\0", map_hostname) && strcmp ("\0", map_ipaddr))
	{
	  /*not duplicated hostname */
	  if ((user_host_Map.find (map_hostname) == user_host_Map.end ()))
	    {
	      user_host_Map[map_hostname] = map_ipaddr;
	      user_host_Map[map_ipaddr] = map_hostname;
	    }
	  /*duplicated hostname */
	  else
	    {
	      if (strcmp (user_host_Map.find (map_hostname)->second.c_str (), map_ipaddr))
		{
		  /*duplicated hostname but different ip address */
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UHOST_HOST_NAME_ALREADY_EXIST, 1, map_hostname);
		  fprintf (stdout, "%s\n", er_msg ());

		  user_host_Map.clear ();

		  fclose (fp);

		  return LOAD_FAIL;

		}
	    }
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UHOST_HOST_NAME_IP_ADDR_NOT_COMPLETE, 0);
	  fprintf (stdout, "%s\n", er_msg ());

	  user_host_Map.clear ();

	  fclose (fp);

	  return LOAD_FAIL;
	}
    }
  fclose (fp);

  return LOAD_SUCCESS;
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
  int ret_val = 0;
  return 0;
#endif

  struct hostent *hp_buf = NULL;
#ifdef HAVE_GETHOSTBYNAME_R
#if defined (HAVE_GETHOSTBYNAME_R_GLIBC)
  int ret_val = 0;
#elif defined (HAVE_GETHOSTBYNAME_R_SOLARIS)
  struct hostent *ret_val = NULL;
#elif defined (HAVE_GETHOSTBYNAME_R_HOSTENT_DATA)
  int ret_val = 0;
#else
#error "HAVE_GETHOSTBYNAME_R"
#endif
#endif

  if (prm_get_bool_value (PRM_ID_USE_USER_HOSTS) == USE_GLIBC_HOSTS)
    {
#ifdef HAVE_GETHOSTBYNAME_R
#if defined (HAVE_GETHOSTBYNAME_R_GLIBC)
      ret_val = gethostbyname_r (name, ret, buf, buflen, result, h_errnop);
#elif defined (HAVE_GETHOSTBYNAME_R_SOLARIS)
      ret_val = gethostbyname_r (name, ret, buf, buflen, h_errnop);
#elif defined (HAVE_GETHOSTBYNAME_R_HOSTENT_DATA)
      ret_val = gethostbyname_r (name, ret, &ht_data);
#else
#error "HAVE_GETHOSTBYNAME_R"
#endif
#endif
      goto return_phase;
    }
  else
    {
      hp_buf = host_lookup_internal (name, NULL, HOSTNAME_TO_IPADDR);
    }

#ifdef HAVE_GETHOSTBYNAME_R
#if defined (HAVE_GETHOSTBYNAME_R_GLIBC)
  if (hp_buf == NULL)
    {
      ret_val = EINVAL;

      goto return_phase;
    }
  if (((*result) = (struct hostent *) malloc (sizeof (struct hostent))) == NULL)
    {
      ret_val = ENOMEM;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (struct hostent));

      goto return_phase;
    }
  memcpy ((void *) ret, (void *) hp_buf, sizeof (struct hostent));
  memcpy ((void *) *result, (void *) hp_buf, sizeof (struct hostent));

  ret_val = 0;
#elif defined (HAVE_GETHOSTBYNAME_R_SOLARIS)
  if (hp_buf == NULL)
    {
//err_print fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR, -ER_FAILED));
      ret_val = NULL;

      goto return_phase;
    }

  memcpy ((void *) ret, (void *) hp_buf, sizeof (struct hostent));

  ret_val = ret;
#elif defined (HAVE_GETHOSTBYNAME_R_HOSTENT_DATA)
  if (hp_buf == NULL)
    {
//err_print fprintf (stdout, msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_ERROR, -ER_FAILED));
      ret_val = -1;

      goto return_phase;
    }

  memcpy ((void *) ret, (void *) hp_buf, sizeof (struct hostent));

  ret_val = 0;
#else
#error "HAVE_GETHOSTBYNAME_R"
#endif /* HAVE_GETHOSTBYNAME_R_GLIBC */
#endif /* HAVE_GETHOSTBYNAME_R */

return_phase:
  return ret_val;
}


int
getnameinfo_uhost (struct sockaddr *addr, socklen_t addrlen, char *host, size_t hostlen, char *serv, size_t servlen,
		   int flags)
{
  int ret;
  struct hostent *hp = NULL;

  if (prm_get_bool_value (PRM_ID_USE_USER_HOSTS) == USE_GLIBC_HOSTS)
    {
      ret = getnameinfo (addr, addrlen, host, hostlen, serv, servlen, flags);
    }
  else
    {
      if ((hp = host_lookup_internal (NULL, addr, IPADDR_TO_HOSTNAME)) != NULL)
	{
	  strncpy (host, hp->h_name, hostlen);
	  host[CUB_MAXHOSTNAMELEN - 1] = '\0';

	  ret = 0;
	}
      else
	{
	  ret = EINVAL;
	}
    }
  return ret;
}

int
getaddrinfo_uhost (char *node, char *service, struct addrinfo *hints, struct addrinfo **res)
{
  int ret = 0;
  struct hostent *hp = NULL;
  struct addrinfo results_out;
  struct sockaddr_in addr_convert;
  struct in_addr *in_addr_buf = NULL;

  if (prm_get_bool_value (PRM_ID_USE_USER_HOSTS) == USE_GLIBC_HOSTS)
    {
      ret = getaddrinfo (node, service, hints, res);
      goto return_phase;
    }
  else
    {
      hp = host_lookup_internal (node, NULL, HOSTNAME_TO_IPADDR);
    }

  if (hp == NULL)
    {
      ret = EAI_NODATA;
      goto return_phase;
    }

  if ((in_addr_buf = (struct in_addr *) malloc (sizeof (struct in_addr))) == NULL)
    {
      FREE_HOSTENT_MEM (hp);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (struct in_addr));
      ret = EAI_MEMORY;
      goto return_phase;
    }

  /*Constitute struct addrinfo for the out parameter res */
  if (((*res) = (struct addrinfo *) malloc (sizeof (struct addrinfo))) == NULL)
    {
      FREE_HOSTENT_MEM (hp);
      free (in_addr_buf);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (struct addrinfo));
      ret = EAI_MEMORY;
      goto return_phase;
    }

  memset (&results_out, 0, sizeof (results_out));
  if ((results_out.ai_addr = (struct sockaddr *) malloc (sizeof (struct sockaddr))) == NULL)
    {
      FREE_HOSTENT_MEM (hp);
      free (in_addr_buf);
      freeaddrinfo (*res);
      free (results_out.ai_addr);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (struct sockaddr));
      ret = EAI_MEMORY;
      goto return_phase;
    }

  if (hints != NULL)
    {
      results_out.ai_flags = hints->ai_flags;
      results_out.ai_family = hints->ai_family;
      results_out.ai_socktype = hints->ai_socktype;
      results_out.ai_protocol = IPPROTO_TCP;
    }
  else
    {
      results_out.ai_flags = (AI_V4MAPPED | AI_ADDRCONFIG);
      results_out.ai_family = AF_UNSPEC;
      results_out.ai_socktype = 0;
      results_out.ai_protocol = 0;
    }


  memcpy (&in_addr_buf->s_addr, hp->h_addr_list[0], sizeof (in_addr_buf->s_addr));
  memcpy (&addr_convert.sin_addr, &in_addr_buf->s_addr, sizeof (addr_convert.sin_addr));
  memcpy (results_out.ai_addr, (struct sockaddr *) &addr_convert, sizeof (struct sockaddr));

  results_out.ai_addrlen = sizeof (results_out.ai_addr);

  results_out.ai_canonname = hp->h_name;

  results_out.ai_next = NULL;

  memmove (*res, &results_out, sizeof (struct addrinfo));

  FREE_MEM (in_addr_buf);
  FREE_MEM (results_out.ai_addr);

return_phase:

  return ret;
}
