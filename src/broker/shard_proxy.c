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
#include "porting.h"
#include "shard_proxy.h"
#include "shard_proxy_handler.h"
#include "shard_key_func.h"

#define ENDLESS 	1

/* SHARD SHM */
int appl_server_shm_id = -1;
T_SHM_APPL_SERVER *shm_as_p = NULL;

int proxy_id = -1;
int proxy_shm_id = -1;
T_SHM_PROXY *shm_proxy_p = NULL;
T_PROXY_INFO *proxy_info_p = NULL;

T_SHM_SHARD_USER *shm_user_p = NULL;
T_SHM_SHARD_KEY *shm_key_p = NULL;
T_SHM_SHARD_CONN *shm_conn_p = NULL;

static int proxy_shm_initialize (void);
/* END OF SHARD SHM */

bool proxy_Keep_running;

static void cleanup (int signo);

static void proxy_set_hang_check_time (void);
static void proxy_unset_hang_check_time (void);

void
proxy_term (void)
{
#ifdef SOLARIS
  SLEEP_MILISEC (1, 0);
#endif
  proxy_handler_destroy ();
  proxy_io_destroy ();
  shard_stmt_destroy ();

  proxy_log_close ();
  proxy_access_log_close ();

  return;
}

static void
cleanup (int signo)
{
  signal (signo, SIG_IGN);

  proxy_term ();
  exit (0);

  return;
}

int
proxy_shm_initialize (void)
{
  char *p;

  p = getenv (PROXY_ID_ENV_STR);
  if (p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to getenv(PROXY_ID_ENV_STR).");
      goto return_error;
    }
  parse_int (&proxy_id, p, 10);

  p = getenv (PROXY_SHM_KEY_STR);
  if (p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to getenv(PROXY_SHM_KEY_STR).");
      goto return_error;
    }
  parse_int (&proxy_shm_id, p, 10);

  shm_proxy_p =
    (T_SHM_PROXY *) uw_shm_open (proxy_shm_id, SHM_PROXY, SHM_MODE_ADMIN);
  if (shm_proxy_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to get shm proxy.");
      goto return_error;
    }

  proxy_info_p = shard_shm_find_proxy_info (shm_proxy_p, proxy_id);
  if (proxy_info_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to get proxy info.");
      goto return_error;
    }

  shm_as_p =
    (T_SHM_APPL_SERVER *) uw_shm_open (proxy_info_p->appl_server_shm_id,
				       SHM_APPL_SERVER, SHM_MODE_ADMIN);
  if (shm_as_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to open shared memory. "
		 "(SHM_APPL_SERVER, shm_key:%d).", appl_server_shm_id);
      goto return_error;
    }

  shm_user_p = shard_metadata_get_user (shm_proxy_p);
  if (shm_user_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to get shm metadata user info.");
      goto return_error;
    }

  shm_key_p = shard_metadata_get_key (shm_proxy_p);
  if (shm_key_p == NULL)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR,
		 "Failed to get shm metadata shard key info.");
      goto return_error;
    }

  shm_conn_p = shard_metadata_get_conn (shm_proxy_p);
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

#if defined(WINDOWS)
  if (wsa_initialize () < 0)
    {
      PROXY_LOG (PROXY_LOG_MODE_ERROR, "Failed to initialize WSA.");
      return error;
    }
#endif

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
  proxy_Keep_running = true;
  while (proxy_Keep_running == true)
    {
      /*
       * Since every operation in proxy main is non-blocking
       * (which is not the case in cas main),
       * proxy_set_hang_check_time is placed at the beginning and
       * proxy_unset_hang_check_time is at the end.
       */
      proxy_set_hang_check_time ();

      /* read or write */
      proxy_io_process ();

      /* process message */
      proxy_handler_process ();

      /* process timer */
      proxy_timer_process ();

      proxy_unset_hang_check_time ();
    }
  PROXY_LOG (PROXY_LOG_MODE_NOTICE, "Shard proxy going down.");

  proxy_term ();
  return 0;
}

static void
proxy_set_hang_check_time ()
{
  if (proxy_info_p != NULL && shm_as_p != NULL && shm_as_p->monitor_hang_flag)
    {
      proxy_info_p->claimed_alive_time = time (NULL);
    }
}

static void
proxy_unset_hang_check_time ()
{
  if (proxy_info_p != NULL && shm_as_p != NULL && shm_as_p->monitor_hang_flag)
    {
      proxy_info_p->claimed_alive_time = (time_t) 0;
    }
}
