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

#define LINE_BUF_SIZE                (512)
#define HOSTNAME_LEN                 (256)
#define MAX_NUM_HOSTS                (256)
#define IPADDR_LEN                   (17)
#define MTIME_DIGITS                 (10)
#define IPv4_ADDR_LEN                (4)
#define NUM_IPADDR_DOT               (3)
#define MAX_NUM_IPADDR_PER_HOST      (1)
#define USER_HOSTS_FILE              "cubrid_hosts.conf"

#define NUM_DIGIT(VAL)              (size_t)(log10 (VAL) + 1)
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
static bool ip_format_check (char *ip_addr);
static int load_hosts_file ();
static struct hostent *host_lookup_internal (const char *hostname, struct sockaddr *saddr, LOOKUP_TYPE lookup_type);
bool check_conf_file_consistency (char *conf_file);

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
  char addr_trans_bi_buf[sizeof (struct in_addr)];

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

  if ((hp->h_addr_list = (char **) malloc (sizeof (char *) * MAX_NUM_IPADDR_PER_HOST)) == NULL)
    {
      FREE_MEM (hp->h_name);
      FREE_MEM (hp);
      return NULL;
    }

  if ((hp->h_addr_list[0] = (char *) malloc (sizeof (char) * IPv4_ADDR_LEN)) == NULL)
    {
      FREE_MEM (hp->h_addr_list);
      FREE_MEM (hp->h_name);
      FREE_MEM (hp);

      return NULL;
    }

  memcpy (hp->h_addr, addr_trans_bi_buf, IPv4_ADDR_LEN);

  return hp;
}


static struct hostent *
host_lookup_internal (const char *hostname, struct sockaddr *saddr, LOOKUP_TYPE lookup_type)
{
  static struct hostent *hp;

  char addr_trans_ch_buf[IPADDR_LEN];
  char hostname_buf[HOSTNAME_LEN];
  char ipaddr_buf[IPADDR_LEN];
  struct sockaddr_in *addr_trans = NULL;

  if (hosts_conf_file_Load == LOAD_INIT)
    {
      pthread_mutex_lock (&load_hosts_file_lock);
      if (hosts_conf_file_Load == LOAD_INIT)
	{
	  hosts_conf_file_Load = load_hosts_file ();
	  if (hosts_conf_file_Load == LOAD_FAIL)
	    {
	      pthread_mutex_unlock (&load_hosts_file_lock);
	      return NULL;
	    }
	}
      pthread_mutex_unlock (&load_hosts_file_lock);
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
  /*line muber of cubrid_hosts.conf file */
  int line_num = 0;

  char addr_trans_ch_buf[IPADDR_LEN];
  struct in_addr addr_trans;

  /*True, when the string token has hostname, otherwise, string token has IP address */
  bool hostent_flag;

  memset (file_line, 0, LINE_BUF_SIZE + 1);

  hosts_conf_dir = envvar_confdir_file (host_conf_file_full_path, PATH_MAX, USER_HOSTS_FILE);

  if (check_conf_file_consistency (host_conf_file_full_path) == 0)
    {
      return LOAD_FAIL;
    }

  fp = fopen (hosts_conf_dir, "r");
  if (fp == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_IO_MOUNT_FAIL, 1, host_conf_file_full_path);
      fprintf (stdout, "%s\n", er_msg ());
      return LOAD_FAIL;
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
	  if (hostent_flag == INSERT_IPADDR)
	    {
	      strcpy (temp_token, token);
	      if (ip_format_check (temp_token) == false)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UHOST_IP_ADDR_INVALID_FORMAT, 3, token, line_num,
			  USER_HOSTS_FILE);
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
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UHOST_HOST_NAME_TOO_LONG, 3, token, line_num,
			  USER_HOSTS_FILE);
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

      if (hostname[0] && ipaddr[0])
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
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UHOST_HOST_NAME_ALREADY_EXIST, 3, hostname, line_num,
			  USER_HOSTS_FILE);
		  fprintf (stdout, "%s\n", er_msg ());

		  user_host_Map.clear ();

		  fclose (fp);

		  return LOAD_FAIL;

		}

	    }
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_UHOST_HOST_NAME_IP_ADDR_NOT_COMPLETE, 2, line_num,
		  USER_HOSTS_FILE);
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
 * gethostbyname_uhost () - Do same job with gethostbyname (), using by the 'user' defined 'cubrid_hosts.conf' file or glibc.
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

bool
check_conf_file_consistency (char *conf_file)
{
  struct stat sbuf;
  int ret_val = 1;
  FILE *fp;
  char latest_mtime[MTIME_DIGITS + 1];
  ssize_t line_len;
  char dir_name[7] = "uhosts";
  char file_name[9] = "date.txt";
  char date_dir_path[PATH_MAX];
  char date_file_path[PATH_MAX];

  envvar_vardir_file (date_dir_path, PATH_MAX, dir_name);

  if (access (date_dir_path, F_OK) < 0)
    {
      /* create directory if not exist */
      if (mkdir (date_dir_path, 0775) < 0 && errno == ENOENT)
	{
	  char pdir[PATH_MAX];

	  if (cub_dirname_r (date_dir_path, pdir, PATH_MAX) > 0 && access (pdir, F_OK) < 0)
	    {
	      mkdir (pdir, 0775);
	    }
	}
    }

  if (access (date_dir_path, F_OK) < 0)
    {
      if (mkdir (date_dir_path, 0775) < 0)
	{
	  ret_val = false;
	  goto return_phase;
	}
    }

  sprintf (date_file_path, "%s/%s", date_dir_path, file_name);
  fp = fopen (date_file_path, "a+");
  if (fp == NULL)
    {
      ret_val = false;
      goto return_phase;
    }

  if (stat (conf_file, &sbuf) >= 0)
    {
      fseek (fp, -(MTIME_DIGITS + 1), SEEK_END);
      line_len = read (fileno (fp), latest_mtime, MTIME_DIGITS + 1);
      latest_mtime[line_len - 1] = '\0';
      if ((atoi (latest_mtime) != sbuf.st_mtime) || (line_len == 0))
	{
	  fprintf (fp, "%d\n", sbuf.st_mtime);
	  if (line_len != 0)
	    {
	      ret_val = false;
	    }
	}
    }
  fclose (fp);

return_phase:

  return ret_val;
}
