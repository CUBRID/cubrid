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
 * shard_proxy.c - 
 *
 */

#ident "$Id$"

#include <signal.h>
#include <errno.h>

/* SHARD TODO : MV include .. */
#include "shard_proxy.h"
#include "shard_proxy_handler.h"
#include "shard_key_func.h"

#define ENDLESS 	1

/* SHARD SHM */
int appl_server_shm_id = -1;
char *shm_as_cp = NULL;
T_SHM_APPL_SERVER *shm_as_p = NULL;

int proxy_id = -1;
T_SHM_PROXY *shm_proxy_p = NULL;
T_PROXY_INFO *proxy_info_p = NULL;

char *shm_metadata_cp = NULL;
T_SHM_SHARD_USER *shm_user_p = NULL;
T_SHM_SHARD_KEY *shm_key_p = NULL;
T_SHM_SHARD_CONN *shm_conn_p = NULL;

static int proxy_shm_initialize (void);
/* END OF SHARD SHM */

static void cleanup (int signo);

void
proxy_term (void)
{
#ifdef SOLARIS
  SLEEP_MILISEC (1, 0);
#endif
  proxy_handler_destroy ();
  proxy_io_destroy ();
  shard_stmt_destroy ();

  exit (0);
}

static void
cleanup (int signo)
{
  signal (signo, SIG_IGN);

  proxy_term ();

  return;
}

int
proxy_shm_initialize (void)
{
  char *p;

  p = getenv (APPL_SERVER_SHM_KEY_STR);
  if (p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to getenv(APPL_SERVER_SHM_KEY_STR).");
      goto return_error;
    }
  appl_server_shm_id = strtoul (p, NULL, 10);

  shm_as_cp =
    (char *) uw_shm_open (appl_server_shm_id, SHM_APPL_SERVER,
			  SHM_MODE_ADMIN);
  if (shm_as_cp == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to open shared memory. "
		 "(SHM_APPL_SERVER, shm_key:%d).", appl_server_shm_id);
      goto return_error;
    }

  p = getenv (PROXY_ID_ENV_STR);
  if (p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to getenv(PROXY_ID_ENV_STR).");
    }
  proxy_id = strtoul (p, NULL, 10);

  shm_as_p = shard_shm_get_appl_server (shm_as_cp);
  if (shm_as_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to get shm application server info.");
      goto return_error;
    }

  shm_proxy_p = shard_shm_get_proxy (shm_as_cp);
  if (shm_proxy_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to get shm proxy info.");
      goto return_error;
    }

  proxy_info_p = shard_shm_get_proxy_info (shm_as_cp, proxy_id);
  if (proxy_info_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to get proxy info.");
      goto return_error;
    }

  shm_metadata_cp =
    (char *) uw_shm_open (shm_proxy_p->metadata_shm_id, SHM_BROKER,
			  SHM_MODE_ADMIN);
  if (shm_metadata_cp == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to open shared memory. "
		 "(SHARD_METADATA, shm_key:%d).",
		 shm_proxy_p->metadata_shm_id);
      goto return_error;
    }

  shm_user_p = shard_metadata_get_user (shm_metadata_cp);
  if (shm_user_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to get shm metadata user info.");
      goto return_error;
    }

  shm_key_p = shard_metadata_get_key (shm_metadata_cp);
  if (shm_key_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to get shm metadata shard key info.");
      goto return_error;
    }
  shm_conn_p = shard_metadata_get_conn (shm_metadata_cp);
  if (shm_conn_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to get shm metadata connection info.");
      goto return_error;
    }

  return 0;

return_error:
  PROXY_LOG (PROXY_LOG_MODE_ERROR,
	     "Failed to initialize shard shared memory.");

  return -1;
}

int
main (int argc, char *argv[])
{
  int error;

  signal (SIGTERM, cleanup);
  signal (SIGINT, cleanup);
#if !defined(WINDOWS)
  signal (SIGPIPE, SIG_IGN);
#endif

  error = proxy_shm_initialize ();
  if (error)
    {
      return error;
    }

  error = register_fn_get_shard_key ();
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to register shard hashing function.");
      return error;
    }

  /* SHARD TODO : initialize IO */
  error = proxy_io_initialize ();
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to initialize proxy IO.");
      return error;
    }

  error = shard_stmt_initialize (proxy_info_p->max_prepared_stmt_count);
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to initialize proxy statement pool.");
      return error;
    }

  error = proxy_handler_initialize ();
  if (error)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to initialize proxy context handler.");
      return error;
    }

  PROXY_LOG (PROXY_LOG_MODE_ERROR, "Shard proxy started.");
  while (ENDLESS)
    {
      /* read or write */
      proxy_io_process ();

      /* process message */
      proxy_handler_process ();
    }
  PROXY_LOG (PROXY_LOG_MODE_NOTICE, "Shard proxy going down.");
  return 0;
}
