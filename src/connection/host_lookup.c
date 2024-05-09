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
#include <io.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#endif

#include "porting.h"
#include "system_parameter.h"
#include "environment_variable.h"
#include "message_catalog.h"
#include "host_lookup.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#define LINE_BUF_SIZE                (512)
#define HOSTNAME_LEN                 (256)
#define MAX_NUM_HOSTS                (256)
#define IPADDR_LEN                   (17)
#define MTIME_DIGITS                 (10)
#define IPv4_ADDR_LEN                (4)
#define NUM_IPADDR_DOT               (3)
#define MAX_NUM_IPADDR_PER_HOST      (1)

#define NUM_DIGIT(VAL)              (size_t)(log10 (VAL) + 1)
#define FREE_MEM(PTR)           \
        do {                    \
          if (PTR) {            \
            free(PTR);          \
            PTR = 0;            \
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

typedef enum
{
  LOAD_FAIL = -1,
  LOAD_INIT,
  LOAD_SUCCESS,
} HOSTS_CONF_LOAD_STATUS;

static struct hostent *hostent_Cache[MAX_NUM_HOSTS];

static int hosts_conf_file_Load = LOAD_INIT;

static pthread_mutex_t load_hosts_file_lock = PTHREAD_MUTEX_INITIALIZER;

// *INDENT-OFF*
static std::unordered_map <std::string, int> user_host_Map;
// *INDENT-ON*

static struct hostent *hostent_alloc (char *ipaddr, char *hostname);
static bool is_valid_ip (char *ip_addr);
static bool is_valid_hostname (char *hostname, int str_len);
static int load_hosts_file ();
static struct hostent *host_lookup_internal (const char *hostname, struct sockaddr *saddr, LOOKUP_TYPE lookup_type);
static void strcpy_ucase (char *dst, const char *src);

/*
 * hostent_alloc () - Allocate memory hostent structure.
 * 
 * return        : The hostent pointer.
 * ipaddr (in)   : Elements of hostent.
 * hostname (in) : Elements of hostent.
 */
static struct hostent *
hostent_alloc (char *ipaddr, char *hostname)
{
  struct hostent *hp;
  char addr_trans_bi_buf[sizeof (struct in_addr)];

  if ((hp = (struct hostent *) malloc (sizeof (struct hostent))) == NULL)
    {
      goto return_phase;
    }

  hp->h_addrtype = AF_INET;
  hp->h_length = IPv4_ADDR_LEN;

  if (inet_pton (AF_INET, ipaddr, addr_trans_bi_buf) < 1)
    {
      FREE_MEM (hp);
      goto return_phase;
    }

  hp->h_name = strdup (hostname);
  hp->h_aliases = NULL;

  if ((hp->h_addr_list = (char **) malloc (sizeof (char *) * MAX_NUM_IPADDR_PER_HOST)) == NULL)
    {
      FREE_MEM (hp->h_name);
      FREE_MEM (hp);
      goto return_phase;
    }

  if ((hp->h_addr_list[0] = (char *) malloc (sizeof (char) * IPv4_ADDR_LEN)) == NULL)
    {
      FREE_MEM (hp->h_addr_list);
      FREE_MEM (hp->h_name);
      FREE_MEM (hp);
      goto return_phase;
    }

  memcpy (hp->h_addr, addr_trans_bi_buf, IPv4_ADDR_LEN);

  return hp;

return_phase:

  return NULL;
}

/*
 * host_lookup_internal () - Look up the hostname or ip address in the hostent_Cache.
 * 
 * return         : The hostent pointer.
 * hostname(in)   : The host name to look up IP address.
 * saddr(in)      : The IP address to look up host name.
 * lookup_type(in): The macro to choose look up mode.
 *  
 *  Note: If lookup_type is HOSTNAME_TO_IPADDR, saddr is NULL. hostname is NULL otherwise.
 */
static struct hostent *
host_lookup_internal (const char *hostname, struct sockaddr *saddr, LOOKUP_TYPE lookup_type)
{
  static struct hostent *hp;

  char addr_trans_ch_buf[IPADDR_LEN];
  struct sockaddr_in *addr_trans = NULL;
  char hostname_u[HOSTNAME_LEN];

  if (hosts_conf_file_Load == LOAD_INIT)
    {
      pthread_mutex_lock (&load_hosts_file_lock);
      if (hosts_conf_file_Load == LOAD_INIT)
	{
	  hosts_conf_file_Load = load_hosts_file ();
	}
      pthread_mutex_unlock (&load_hosts_file_lock);
    }
  if (hosts_conf_file_Load == LOAD_FAIL)
    {
      goto return_phase;
    }

  addr_trans = (struct sockaddr_in *) saddr;

  assert (((hostname != NULL) && (saddr == NULL)) || ((hostname == NULL) && (saddr != NULL)));

  if (lookup_type == IPADDR_TO_HOSTNAME)
    {
      if (inet_ntop (AF_INET, &addr_trans->sin_addr, addr_trans_ch_buf, sizeof (addr_trans_ch_buf)) == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
	  goto return_phase;
	}

    }

  strcpy_ucase (hostname_u, hostname);

  /* Look up in the user_host_Map */
  /* The case which is looking up the IP addr and checking the hostname or IP addr in the hash map */
  if ((lookup_type == HOSTNAME_TO_IPADDR) && (user_host_Map.find (hostname_u) != user_host_Map.end ()))
    {
      hp = hostent_Cache[user_host_Map.find (hostname_u)->second];
    }
  else if ((lookup_type == IPADDR_TO_HOSTNAME) && (user_host_Map.find (addr_trans_ch_buf) != user_host_Map.end ()))
    {
      hp = hostent_Cache[user_host_Map.find (addr_trans_ch_buf)->second];
    }
  /*Hostname and IP addr cannot be found */
  else
    {
      goto return_phase;
    }

  return hp;

return_phase:

  return NULL;
}

/*
 * load_hosts_file () - Load the cubrid_host.conf and be ready for using user_host_Map and hostent_Cache.
 * 
 * return   : The result of cubrid_host.conf loading.
 */
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
  /*line muber of cubrid_hosts.conf file */
  int line_num = 0;
  int str_len = 0;

  char addr_trans_ch_buf[IPADDR_LEN];
  struct in_addr addr_trans;

  /*True, when the string token has hostname, otherwise, string token has IP address */
  bool hostent_flag;

  memset (file_line, 0, LINE_BUF_SIZE + 1);

  hosts_conf_dir = envvar_confdir_file (host_conf_file_full_path, PATH_MAX, "cubrid_hosts.conf");

  fp = fopen (hosts_conf_dir, "r");
  if (fp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_FILE_NOT_FOUND, 1, host_conf_file_full_path);
      goto load_end_phase;
    }

  while (fgets (file_line, LINE_BUF_SIZE, fp) != NULL)
    {
      line_num++;
      hostname[0] = '\0';
      ipaddr[0] = '\0';
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
	  strcpy (temp_token, token);
	  if (hostent_flag == INSERT_IPADDR)
	    {
	      if (is_valid_ip (temp_token) == false)
		{
		  continue;
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
	      if (hostname[0] != '\0')
		{
		  break;
		}

	      str_len = strlen (temp_token);
	      if (str_len > HOSTNAME_LEN - 1)
		{
		  continue;
		}
	      else if (is_valid_ip (temp_token) == true)
		{
		  continue;
		}
	      else if (is_valid_hostname (temp_token, str_len) == false)
		{
		  continue;
		}
	      else
		{
		  strcpy_ucase (hostname, token);
		}
	    }
	}
      while ((token = strtok_r (NULL, delim, &save_ptr_strtok)) != NULL);

      if (hostname[0] && ipaddr[0])
	{
	  /*not duplicated hostname, IP address or not duplicated hostname and duplicated IP address */
	  if ((user_host_Map.find (hostname) == user_host_Map.end ())
	      && (user_host_Map.find (ipaddr) == user_host_Map.end ()))
	    {
	      user_host_Map[hostname] = cache_idx;
	      user_host_Map[ipaddr] = cache_idx;
	      if ((hostent_Cache[cache_idx] = hostent_alloc (ipaddr, hostname)) == NULL)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
		  user_host_Map.clear ();
		  /*to return the 'LOAD_FAIL' */
		  cache_idx = 0;
		  goto load_end_phase;
		}

	      cache_idx++;
	    }
	  /*duplicated hostname */
	  else
	    {
	      continue;
	    }
	}
    }

load_end_phase:
  if (fp != NULL)
    {
      fclose (fp);
    }

  return cache_idx > 0 ? LOAD_SUCCESS : LOAD_FAIL;
}

/*
 * is_valid_ip () - Check the ipv4 IP address format.
 * 
 * return   : true if IP address is valid format, false otherwise
 */
static bool
is_valid_ip (char *ip_addr)
{

  int dec_val;
  bool ret = true;
  int dot = -1;
  char *token;
  char *save_ptr_strtok;
  const char *delim = " .\n";

  if ((token = strtok_r (ip_addr, delim, &save_ptr_strtok)) == NULL)
    {
      goto err_phase;
    }

  do
    {
      dec_val = atoi (token);
      if (dec_val < 0 || dec_val > 255)
	{
	  goto err_phase;
	}
      else if (dec_val == 0 && token[0] != '0')
	{
	  goto err_phase;
	}
      else if (dec_val != 0 && (NUM_DIGIT (dec_val) != strlen (token)))
	{
	  goto err_phase;
	}
      else
	{
	  dot++;
	}
    }
  while (token = strtok_r (NULL, delim, &save_ptr_strtok));
  if (dot != NUM_IPADDR_DOT)
    {
      goto err_phase;
    }

  return ret;

err_phase:

  ret = false;
  return ret;
}

/*
 * is_valid_hostname () - Check the host name is valid format.
 *
 * return   : true if host name is valid format, false otherwise
 */
static bool
is_valid_hostname (char *hostname, int str_len)
{

  int char_num = 0;

  if (!isalpha (hostname[0]))
    {
      return false;
    }

  if (!isalnum (hostname[str_len - 1]))
    {
      return false;
    }

  for (char_num = 1; char_num < str_len - 1; char_num++)
    {
      if (!(isalnum (hostname[char_num]) || (hostname[char_num] == '-') || (hostname[char_num] == '.')))
	{
	  return false;
	}
    }

  return true;
}

/*
 * gethostbyname_uhost () - Do same job with gethostbyname (), using by the 'user' defined 'cubrid_hosts.conf' file or glibc.
 * 
 * return   : the hostent pointer.
 *
 *  Note: The parameters are same with glibc parameters.
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

/*
 * gethostbyname_r_uhost () - Do same job with gethostbyname_r (), using by the 'user' defined 'cubrid_hosts.conf' file or glibc.
 * 
 *  Note: The parameters are same with glibc parameters.
 */
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
      ret_val = NULL;

      goto return_phase;
    }

  memcpy ((void *) ret, (void *) hp_buf, sizeof (struct hostent));

  ret_val = ret;
#elif defined (HAVE_GETHOSTBYNAME_R_HOSTENT_DATA)
  if (hp_buf == NULL)
    {
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

/*
 * getnameinfo_uhost () - Do same job with getnameinfo (), using by the 'user' defined 'cubrid_hosts.conf' file or glibc.
 * 
 * return   : 0, if successful, Error otherwise.
 *
 *  Note: The parameters are same with glibc parameters.
 */
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

/*
 * getaddrinfo_uhost () - Do same job with getaddrinfo (), using by the 'user' defined 'cubrid_hosts.conf' file or glibc.
 * 
 * return   : 0, if successful, Error otherwise.
 *
 *  Note: The parameters are same with glibc parameters.
 */
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
      addrp->ai_family = hints->ai_family;
    }
  else
    {
      addrp->ai_family = AF_UNSPEC;
    }

  *res = addrp;

return_phase:

  return ret;
}

static void
strcpy_ucase (char *dst, const char *src)
{
  int len, i;

  if (dst == NULL || src == NULL)
    {
      return;
    }

  len = strlen (src) > (HOSTNAME_LEN - 1) ? (HOSTNAME_LEN - 1) : strlen (src);

  for (i = 0; i < len; i++)
    {
      dst[i] = toupper (src[i]);
    }

  dst[i] = '\0';

  return;
}
