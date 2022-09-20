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
#include <stdlib.h>
#include <math.h>
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
#include "message_catalog.h"

#define HOSTNAME_LEN                 (256)
#define MAX_NUM_HOSTS                (256)
#define LINE_BUF_SIZE                (512)
#define IPADDR_LEN                   (17)
#define NUM_IPADDR_DOT               (3)
#define IPv4_ADDR_LEN                (4)

#define NUM_DIGIT(VAL)              (size_t)(log10 (VAL) + 1)
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

static const char *user_hosts_File = "hosts.conf";
static struct hostent *hostent_Cache[MAX_NUM_HOSTS];

static int hosts_conf_file_Load = LOAD_INIT;

// *INDENT-OFF*
static std::unordered_map <std::string, int> user_host_Map;
// *INDENT-ON*

static struct hostent *hostent_alloc (char *ipaddr, char *hostname);
static bool ip_format_check (char *ip_addr);
static int load_hosts_file ();
static struct hostent *host_lookup_internal (const char *hostname, struct sockaddr *saddr, LOOKUP_TYPE lookup_type);

/*
 * hostent_alloc () - Allocate memory hostent structure.
 * 
 * return   : the hostent pointer.
 *
 */
static struct hostent *
hostent_alloc (char *ipaddr, char *hostname)
{
  struct hostent *hp;
  char addr_trans_bi_buf[IPADDR_LEN];

  if ((hp = (struct hostent *) malloc (sizeof (struct hostent))) == NULL)
    {
      return NULL;
    }

  hp->h_addrtype = AF_INET;
  hp->h_length = IPv4_ADDR_LEN;

  if (inet_pton (AF_INET, ipaddr, addr_trans_bi_buf) < 1)
    {
      FREE_MEM (hp);
      return NULL;
    }

  hp->h_name = strdup (hostname);
  hp->h_aliases = NULL;

  if ((hp->h_addr_list = (char **) malloc (sizeof (char *) * MAX_NUM_HOSTS)) == NULL)
    {
      FREE_MEM (hp->h_name);
      FREE_MEM (hp);
      return NULL;
    }

  if ((hp->h_addr_list[0] = (char *) malloc (sizeof (char) * IPADDR_LEN)) == NULL)
    {
      FREE_MEM (hp->h_addr_list);
      FREE_MEM (hp->h_name);
      FREE_MEM (hp);

      return NULL;
    }

  memcpy (hp->h_addr, addr_trans_bi_buf, 4);

  return hp;
}


static struct hostent *
host_lookup_internal (const char *hostname, struct sockaddr *saddr, LOOKUP_TYPE lookup_type)
{
  static struct hostent *hp;
  int i, find_index = -1;

  char addr_trans_ch_buf[IPADDR_LEN];
  char addr_trans_bi_buf[IPADDR_LEN];
  char hostname_buf[HOSTNAME_LEN];
  char ipaddr_buf[IPADDR_LEN];
  struct sockaddr_in *addr_trans = NULL;

  if (hosts_conf_file_Load == LOAD_INIT)
    {
      if ((hosts_conf_file_Load = load_hosts_file ()) == LOAD_FAIL)
	{
	  return NULL;
	}
    }

  addr_trans = (struct sockaddr_in *) saddr;

  assert (((hostname != NULL) && (saddr == NULL)) || ((hostname == NULL) && (saddr != NULL)));

  if (lookup_type == IPADDR_TO_HOSTNAME)
    {
      if (inet_ntop (AF_INET, &addr_trans->sin_addr, addr_trans_ch_buf, sizeof (addr_trans_ch_buf)) == NULL)
	{
	  return NULL;
	}

    }

  /*Look up in the user_host_Map */
  if ((lookup_type == HOSTNAME_TO_IPADDR) && (user_host_Map.find (hostname) != user_host_Map.end ()))
    {
      hp = hostent_Cache[user_host_Map.find (hostname)->second];
    }
  else if ((lookup_type == IPADDR_TO_HOSTNAME) && (user_host_Map.find (addr_trans_ch_buf) != user_host_Map.end ()))
    {
      hp = hostent_Cache[user_host_Map.find (addr_trans_ch_buf)->second];
    }
  else
    {
      if (lookup_type == HOSTNAME_TO_IPADDR)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UHOST_CANT_LOOKUP_INFO, 1, hostname);
	  fprintf (stdout, "%s\n", er_msg ());
	}
      return NULL;
    }

  return hp;
}

static int
load_hosts_file ()
{
  FILE *fp;
  char file_line[LINE_BUF_SIZE + 1];
  char host_conf_file_full_path[PATH_MAX];
  char *hosts_conf_dir;

  char *token, temp_token[LINE_BUF_SIZE + 1];
  char *save_ptr_strtok;
  /*delimiter */
  const char *delim = " \t\n";
  char ipaddr[IPADDR_LEN];
  char hostname[HOSTNAME_LEN];
  int cache_idx = 0, temp_idx;

  char addr_trans_ch_buf[IPADDR_LEN];
  struct in_addr addr_trans;

  /*True, when the string token has hostname, otherwise, string token has IP address */
  bool hostent_flag;

  memset (file_line, 0, LINE_BUF_SIZE);

  hosts_conf_dir = envvar_confdir_file (host_conf_file_full_path, PATH_MAX, user_hosts_File);
  fp = fopen (hosts_conf_dir, "r");
  if (fp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, host_conf_file_full_path);
      fprintf (stdout, "%s\n", er_msg ());
      return LOAD_FAIL;
    }

  while (fgets (file_line, LINE_BUF_SIZE, fp) != NULL)
    {
      if (file_line[0] == '#')
	continue;
      if (file_line[0] == '\n')
	continue;

      token = strtok_r (file_line, delim, &save_ptr_strtok);
      hostent_flag = INSERT_IPADDR;

      do
	{
	  if (token == NULL || *token == '#')
	    {
	      break;
	    }
	  if (hostent_flag == INSERT_IPADDR)
	    {
	      strcpy (temp_token, token);
	      if (ip_format_check (temp_token) == false)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UHOST_IP_ADDR_INVALID_FORMAT, 1, token);
		  fprintf (stdout, "%s\n", er_msg ());

		  user_host_Map.clear ();

		  fclose (fp);

		  return LOAD_FAIL;
		}
	      else
		{
		  strncpy (ipaddr, token, IPADDR_LEN);
		  ipaddr[IPADDR_LEN - 1] = '\0';

		  hostent_flag = INSERT_HOSTNAME;
		}
	    }
	  else
	    {
	      if (strlen (token) > HOSTNAME_LEN - 1)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UHOST_HOST_NAME_TOO_LONG, 1, token);
		  fprintf (stdout, "%s\n", er_msg ());

		  user_host_Map.clear ();

		  fclose (fp);

		  return LOAD_FAIL;
		}
	      else
		{
		  strcpy (hostname, token);
		}
	    }
	}
      while ((token = strtok_r (NULL, delim, &save_ptr_strtok)) != NULL);

      if (strcmp ("\0", hostname) && strcmp ("\0", ipaddr))
	{
	  /*not duplicated hostname */
	  if ((user_host_Map.find (hostname) == user_host_Map.end ()))
	    {
	      user_host_Map[hostname] = cache_idx;
	      user_host_Map[ipaddr] = cache_idx;
	      if ((hostent_Cache[cache_idx] = hostent_alloc (ipaddr, hostname)) == NULL)
		{
		  fclose (fp);
		  return LOAD_FAIL;
		}

	      cache_idx++;
	    }
	  /*duplicated hostname */
	  else
	    {
	      /*duplicated hostname but different ip address */

	      temp_idx = user_host_Map.find (hostname)->second;
	      memcpy (&addr_trans.s_addr, hostent_Cache[temp_idx]->h_addr_list[0], sizeof (addr_trans.s_addr));

	      if (inet_ntop (AF_INET, &addr_trans.s_addr, addr_trans_ch_buf, sizeof (addr_trans_ch_buf)) == NULL)
		{
		  fclose (fp);
		  return LOAD_FAIL;
		}

	      if (strcmp (addr_trans_ch_buf, ipaddr))
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UHOST_HOST_NAME_ALREADY_EXIST, 1, hostname);
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


static bool
ip_format_check (char *ip_addr)
{

  int dec_val;
  bool ret = true;
  int dot = -1;
  char *token;
  char *save_ptr_strtok;
  const char *delim = " .\n";

  if ((token = strtok_r (ip_addr, delim, &save_ptr_strtok)) == NULL)
    {
      ret = false;
    }

  do
    {
      dec_val = atoi (token);
      if (dec_val < 0 || dec_val > 255)
	{
	  ret = false;
	  break;
	}
      else if (dec_val == 0 && token[0] != '0')
	{
	  ret = false;
	  break;
	}
      else if (dec_val != 0 && (NUM_DIGIT (dec_val) != strlen (token)))
	{
	  ret = false;
	  break;
	}
      else
	{
	  dot++;
	}
    }
  while (token = strtok_r (NULL, delim, &save_ptr_strtok));
  if (dot != NUM_IPADDR_DOT)
    {
      ret = false;
    }

  return ret;
}

/*
 * gethostbyname_uhost () - Do same job with gethostbyname (), using by the 'user' defined 'hosts.conf' file.
 * 
 * return   : the hostent pointer.
 * hostname (in) : the hostname.
 *
 */
struct hostent *
gethostbyname_uhost (const char *name)
{
  static struct hostent *hp = NULL;

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

  memcpy ((void *) ret, (void *) hp_buf, sizeof (struct hostent));
  *result = hp_buf;

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
	  host[hostlen] = '\0';

	  ret = 0;
	}
      else
	{
	  ret = EAI_NONAME;
	}
    }
  return ret;
}

int
getaddrinfo_uhost (char *node, char *service, struct addrinfo *hints, struct addrinfo **res)
{
  int ret = 0;
  struct hostent *hp = NULL;
  struct addrinfo *addrp;

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
      ret = EAI_NONAME;
      goto return_phase;
    }

  if ((addrp = (struct addrinfo *) malloc (sizeof (struct addrinfo))) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (struct addrinfo));
      ret = EAI_MEMORY;
      goto return_phase;
    }

  memset (addrp, 0, sizeof (addrinfo));
  if ((addrp->ai_canonname = strdup (hp->h_name)) == NULL)
    {
      freeaddrinfo (addrp);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (struct sockaddr));
      ret = EAI_MEMORY;
      goto return_phase;
    }

  if (hints != NULL)
    {
      addrp->ai_flags = hints->ai_flags;
      addrp->ai_family = hints->ai_family;
      addrp->ai_socktype = hints->ai_socktype;
      addrp->ai_protocol = IPPROTO_TCP;
    }
  else
    {
      addrp->ai_flags = (AI_V4MAPPED | AI_ADDRCONFIG);
      addrp->ai_family = AF_UNSPEC;
    }

  *res = addrp;


return_phase:

  return ret;
}
