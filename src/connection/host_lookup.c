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

#include "host_lookup.h"
#include "porting.h"
#include "system_parameter.h"
#include "environment_variable.h"
#include "message_catalog.h"
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

#if !defined (MALLOC)
#define MALLOC(SIZE)            malloc(SIZE)
#define FREE_MEM(PTR)           \
        do {                    \
          if (PTR) {            \
            free(PTR);          \
            PTR = 0;            \
          }                     \
        } while (0)
#endif

#define CUBRID_HOSTS_CONF            "cubrid_hosts.conf"
#define MAX_FQDN_LEN                 (255)
#define MAX_LABEL_LEN                (63)

#define ADD_ENTRY_SUCCESS            0	// Successfully added an entry
#define DUPLICATE_ENTRY              1	// Entry is a duplicate
#define ERROR_ALLOC                 -1	// Memory allocation error or general failure

#define UHOST_CONF_VALID_CHECK       1
#define UHOST_CONF_LOAD              2

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

static struct hostent *hostent_alloc (const char *ipaddr, const char *hostname);
static int load_hosts_file (void);
static struct hostent *host_lookup_internal (const char *hostname, struct sockaddr *saddr, LOOKUP_TYPE lookup_type);
static void strcpy_ucase (char *dst, size_t len, const char *src);
static bool is_valid_ipv4 (const char *ip_addr);
static int is_valid_label (const char *label);
static int is_valid_fqdn (const char *fqdn);
static int add_user_host_map (const char *ipaddr, const char *hostname, int cache_idx);
static bool handle_uhost_conf (int action_type);

/*
 * hostent_alloc () - Allocate memory hostent structure.
 * 
 * return        : The hostent pointer.
 * ipaddr (in)   : Elements of hostent.
 * hostname (in) : Elements of hostent.
 */
static struct hostent *
hostent_alloc (const char *ipaddr, const char *hostname)
{
  struct hostent *hp;
  char addr_trans_bi_buf[sizeof (struct in_addr)];

  if ((hp = (struct hostent *) MALLOC (sizeof (struct hostent))) == NULL)
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

  if ((hp->h_addr_list = (char **) MALLOC (sizeof (char *) * MAX_NUM_IPADDR_PER_HOST)) == NULL)
    {
      FREE_MEM (hp->h_name);
      FREE_MEM (hp);
      goto return_phase;
    }

  if ((hp->h_addr_list[0] = (char *) MALLOC (sizeof (char) * IPv4_ADDR_LEN)) == NULL)
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

  strcpy_ucase (hostname_u, (size_t) HOSTNAME_LEN, hostname);

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

  if ((addrp = (struct addrinfo *) MALLOC (sizeof (struct addrinfo))) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, sizeof (struct addrinfo));
      ret = EAI_MEMORY;
      goto return_phase;
    }

  memset (addrp, 0, sizeof (addrinfo));
  if ((addrp->ai_canonname = strdup (hp->h_name)) == NULL)
    {
      freeaddrinfo_uhost (addrp);
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

void
freeaddrinfo_uhost (struct addrinfo *res)
{
  if (prm_get_bool_value (PRM_ID_USE_USER_HOSTS) == USE_GLIBC_HOSTS)
    {
      return (freeaddrinfo (res));
    }

  if (res)
    {
      FREE_MEM (res->ai_canonname);
      FREE_MEM (res);
    }
  return;
}

static void
strcpy_ucase (char *dst, size_t len, const char *src)
{
  int i;

  if (dst == NULL || src == NULL || len == 0)
    {
      return;
    }

  for (i = 0; i < len - 1; i++)
    {
      dst[i] = (char) toupper (src[i]);
    }
  dst[i] = '\0';

  return;
}


/*
* is_valid_ip () - Check the ipv4 IP address format.
* 
* return   : true if IP address is valid format, false otherwise
*/
static bool
is_valid_ipv4 (const char *ip_addr)
{
  struct sockaddr_in sa;

  if (ip_addr == NULL || *ip_addr == '\0')
    {
      return false;
    }

  return inet_pton (AF_INET, ip_addr, &(sa.sin_addr)) == 1;
}


/*
* is_valid_hostname () - Check the host name is valid format.
*
* return   : true if host name is valid format, false otherwise
*/
static int
is_valid_label (const char *label)
{
  int length = 0;

  if (label == NULL || *label == '\0')
    {
      return 0;
    }

  length = strlen (label);
  if (length > MAX_LABEL_LEN)
    {
      return 0;
    }

  if (!isalpha (label[0]) || !isalnum (label[length - 1]))
    {
      return 0;
    }

  for (int i = 0; i < length; i++)
    {
      if (!isalnum (label[i]) && label[i] != '-')
	{
	  return 0;
	}
    }

  return 1;
}

/**
*  Checks if a given string is a valid Fully Qualified Domain Name (FQDN).
*
* An FQDN must:
* - Not exceed 253 characters in length.
* - Have labels that are 1 to 63 characters long.
* - Contain only alphanumeric characters and hyphens.
* - Not start or end with a hyphen in any label.
* - Not be empty or NULL.
*
*/
static int
is_valid_fqdn (const char *fqdn)
{
  char label[MAX_LABEL_LEN + 1];
  int label_len = 0;
  int length = 0;

  if (fqdn == NULL || *fqdn == '\0')
    {
      return 0;
    }

  length = strlen (fqdn);

  if (length > MAX_FQDN_LEN)
    {
      return 0;
    }

  for (int i = 0; i < length; i++)
    {
      if (fqdn[i] == '.')
	{
	  label[label_len] = '\0';
	  if (!is_valid_label (label))
	    {
	      return 0;
	    }
	  label_len = 0;
	}
      else
	{
	  if (label_len >= MAX_LABEL_LEN)
	    {
	      return 0;
	    }
	  label[label_len++] = fqdn[i];
	}
    }

  label[label_len] = '\0';
  return is_valid_label (label);
}

bool
validate_uhost_conf (void)
{
  return handle_uhost_conf (UHOST_CONF_VALID_CHECK);
}

/*
* load_hosts_file () - Load the cubrid_host.conf and be ready for using user_host_Map and hostent_Cache.
* 
* return   : The result of cubrid_host.conf loading.
*/
static int
load_hosts_file (void)
{
  return handle_uhost_conf (UHOST_CONF_LOAD) ? LOAD_SUCCESS : LOAD_FAIL;
}

/**
*
* This function processes the `uhosts.conf` file based on the specified `action_type`. It can either:
* 1. Load the IP addresses and hostnames from the configuration file into the `user_host_Map`.
* 2. Check the validity of the IP addresses and hostnames in the configuration file.
*
* - UHOST_CONF_VALID_CHECK: Validates the IP addresses and hostnames in the cubrid_uhosts.conf file. 
* - UHOST_CONF_LOAD: Loads the valid IP addresses and hostnames.
*
*/
static bool
handle_uhost_conf (int action_type)
{
  FILE *file = NULL;
  char line_buf[LINE_BUF_SIZE + 1];
  char *hosts_conf_dir;
  char host_conf_file_full_path[PATH_MAX];
  char ipaddr[IPADDR_LEN];
  char hostname[HOSTNAME_LEN];
  char *save_ptr_strtok;
  int line_number = 0;
  bool is_empty_hostinfo = true;
  int cache_idx = 0;
  bool has_invalid_entries = false;

  hosts_conf_dir = envvar_confdir_file (host_conf_file_full_path, PATH_MAX, CUBRID_HOSTS_CONF);

  file = fopen (hosts_conf_dir, "r");
  if (!file)
    {
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS, PRM_ERR_FILE_NOT_FOUND),
	       host_conf_file_full_path);
      return false;
    }

  while (fgets (line_buf, sizeof (line_buf), file))
    {
      char *hash_pos;

      line_number++;

      hash_pos = strchr (line_buf, '#');
      if (hash_pos != NULL)
	{
	  *hash_pos = '\0';
	}

      trim (line_buf);

      if (line_buf[0] == '\0')
	{
	  continue;
	}

      is_empty_hostinfo = false;

      char *token = strtok_r (line_buf, " \t\n", &save_ptr_strtok);
      if (token != NULL)
	{
	  strncpy (ipaddr, token, IPADDR_LEN - 1);
	  ipaddr[IPADDR_LEN - 1] = '\0';

	  token = strtok_r (NULL, "\n", &save_ptr_strtok);
	  if (token != NULL)
	    {
	      trim (token);
	      strcpy_ucase (hostname, (size_t) HOSTNAME_LEN, token);
	    }
	  else
	    {
	      // No hostname case
	      if (action_type == UHOST_CONF_VALID_CHECK)
		{
		  if (!is_valid_ipv4 (ipaddr))
		    {
		      fprintf (stderr,
			       msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS,
					       PRM_ERR_INVALID_HOSTNAME), ipaddr, line_number, hosts_conf_dir);
		    }
		  else
		    {
		      fprintf (stderr,
			       msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS,
					       PRM_ERR_INVALID_HOSTNAME), "No Hostname", line_number, hosts_conf_dir);
		    }
		  has_invalid_entries = true;
		  goto end;
		}
	      continue;
	    }

	  // Validate IP address
	  if (!is_valid_ipv4 (ipaddr))
	    {
	      if (action_type == UHOST_CONF_VALID_CHECK)
		{
		  fprintf (stderr,
			   msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS,
					   PRM_ERR_INVALID_HOSTNAME), ipaddr, line_number, hosts_conf_dir);
		  has_invalid_entries = true;
		  goto end;
		}
	      continue;
	    }

	  // Validate hostname
	  if (!is_valid_fqdn (hostname))
	    {
	      if (action_type == UHOST_CONF_VALID_CHECK)
		{
		  fprintf (stderr,
			   msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS,
					   PRM_ERR_INVALID_HOSTNAME), hostname, line_number, hosts_conf_dir);
		  has_invalid_entries = true;
		  goto end;
		}
	      continue;
	    }

	  if (action_type == UHOST_CONF_LOAD)
	    {
	      // Add valid entries to user_host_Map
	      if (hostname[0] && ipaddr[0])
		{
		  int result = add_user_host_map (ipaddr, hostname, cache_idx);

		  if (result == ADD_ENTRY_SUCCESS)
		    {
		      cache_idx++;
		    }
		  else if (result == DUPLICATE_ENTRY)
		    {
		      continue;
		    }
		  else if (result == ERROR_ALLOC)
		    {
		      cache_idx = 0;
		      goto end;
		    }
		}
	    }
	}
    }

  // Check if the host info is empty
  if (action_type == UHOST_CONF_VALID_CHECK && is_empty_hostinfo == true)
    {
      fprintf (stderr,
	       msgcat_message (MSGCAT_CATALOG_CUBRID, MSGCAT_SET_PARAMETERS, PRM_ERR_EMPTY_HOSTS_CONF), hosts_conf_dir);
      has_invalid_entries = true;
    }

end:
  if (file)
    {
      fclose (file);
    }

  if (action_type == UHOST_CONF_VALID_CHECK && has_invalid_entries)
    {
      return false;
    }

  if (action_type == UHOST_CONF_LOAD && (cache_idx == 0))
    {
      return false;
    }

  return true;
}

/**
* Adds a valid IP and hostname to the user_host_Map and updates the cache.
*/
static int
add_user_host_map (const char *ipaddr, const char *hostname, int cache_idx)
{
  /*not duplicated hostname, IP address or not duplicated hostname and duplicated IP address */
  if (user_host_Map.find (hostname) != user_host_Map.end () || user_host_Map.find (ipaddr) != user_host_Map.end ())
    {
      return DUPLICATE_ENTRY;
    }

  user_host_Map[hostname] = cache_idx;
  user_host_Map[ipaddr] = cache_idx;

  if ((hostent_Cache[cache_idx] = hostent_alloc (ipaddr, hostname)) == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      user_host_Map.clear ();
      return ERROR_ALLOC;
    }

  return ADD_ENTRY_SUCCESS;
}
