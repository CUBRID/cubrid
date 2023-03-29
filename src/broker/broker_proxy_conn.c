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
 * broker_proxy_conn.c -
 */

#ident "$Id$"

#include <sys/types.h>
#include <assert.h>
#if !defined(WINDOWS)
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#endif /* !WINDOWS */

#include "porting.h"
#include "broker_proxy_conn.h"
#include "shard_proxy_common.h"
#include "shard_shm.h"

#if !defined(WINDOWS)
T_PROXY_CONN broker_Proxy_conn = {
  -1,				/* max_num_proxy */
  0,				/* cur_num_proxy */
  NULL				/* proxy_sockfd */
};

#define	PROXY_SVR_CON_RETRY_COUNT	3
#define	PROXY_SVR_CON_RETRY_MSEC	400

pthread_mutex_t proxy_conn_mutex;

static void broker_free_all_proxy_conn_ent (void);
static T_PROXY_CONN_ENT *broker_find_proxy_conn_by_fd (SOCKET fd);
static T_PROXY_CONN_ENT *broker_find_proxy_conn_by_id (int proxy_id);

static void
broker_free_all_proxy_conn_ent (void)
{
  T_PROXY_CONN_ENT *ent_p, *next_ent_p;

  for (ent_p = broker_Proxy_conn.proxy_conn_ent; ent_p; ent_p = next_ent_p)
    {
      next_ent_p = ent_p->next;

      FREE_MEM (ent_p);
      ent_p = NULL;
    }

  broker_Proxy_conn.proxy_conn_ent = NULL;
}

int
broker_set_proxy_fds (fd_set * fds)
{
  int ret = 0;
  T_PROXY_CONN_ENT *ent_p;

  pthread_mutex_lock (&proxy_conn_mutex);
  for (ent_p = broker_Proxy_conn.proxy_conn_ent; ent_p; ent_p = ent_p->next)
    {
      if (ent_p->fd != INVALID_SOCKET && ent_p->status == PROXY_CONN_CONNECTED)
	{
	  FD_SET (ent_p->fd, fds);
	}
    }
  pthread_mutex_unlock (&proxy_conn_mutex);

  return ret;
}

SOCKET
broker_get_readable_proxy_conn (fd_set * fds)
{
  SOCKET fd;
  T_PROXY_CONN_ENT *ent_p;

  pthread_mutex_lock (&proxy_conn_mutex);
  for (ent_p = broker_Proxy_conn.proxy_conn_ent; ent_p; ent_p = ent_p->next)
    {
      if (ent_p->fd == INVALID_SOCKET)
	{
	  continue;
	}

      if (ent_p->status != PROXY_CONN_CONNECTED)
	{
	  continue;
	}

      if (FD_ISSET (ent_p->fd, fds))
	{
	  fd = ent_p->fd;
	  FD_CLR (ent_p->fd, fds);

	  pthread_mutex_unlock (&proxy_conn_mutex);
	  return fd;
	}
    }
  pthread_mutex_unlock (&proxy_conn_mutex);

  return INVALID_SOCKET;

}

static T_PROXY_CONN_ENT *
broker_find_proxy_conn_by_fd (SOCKET fd)
{
  T_PROXY_CONN_ENT *ent_p;

  for (ent_p = broker_Proxy_conn.proxy_conn_ent; ent_p; ent_p = ent_p->next)
    {
      if (ent_p->fd != fd)
	{
	  continue;
	}

      return ent_p;
    }

  return NULL;
}

static T_PROXY_CONN_ENT *
broker_find_proxy_conn_by_id (int proxy_id)
{
  T_PROXY_CONN_ENT *ent_p;

  for (ent_p = broker_Proxy_conn.proxy_conn_ent; ent_p; ent_p = ent_p->next)
    {
      if (ent_p->proxy_id != proxy_id)
	{
	  continue;
	}

      return ent_p;
    }

  return NULL;
}

int
broker_add_proxy_conn (SOCKET fd)
{
  int ret = 0;
  T_PROXY_CONN_ENT *ent_p;

  if (broker_Proxy_conn.max_num_proxy < 0)
    {
      return -1;
    }

  pthread_mutex_lock (&proxy_conn_mutex);
  if (broker_Proxy_conn.max_num_proxy <= broker_Proxy_conn.cur_num_proxy)
    {
      pthread_mutex_unlock (&proxy_conn_mutex);
      return -1;
    }

  ent_p = (T_PROXY_CONN_ENT *) malloc (sizeof (T_PROXY_CONN_ENT));
  if (ent_p == NULL)
    {
      pthread_mutex_unlock (&proxy_conn_mutex);
      return -1;
    }

  ent_p->proxy_id = PROXY_INVALID_ID;
  ent_p->status = PROXY_CONN_CONNECTED;
  ent_p->fd = fd;

  ent_p->next = broker_Proxy_conn.proxy_conn_ent;
  broker_Proxy_conn.proxy_conn_ent = ent_p;

  broker_Proxy_conn.cur_num_proxy++;
  if (broker_Proxy_conn.cur_num_proxy > broker_Proxy_conn.max_num_proxy)
    {
      assert (false);
      broker_Proxy_conn.cur_num_proxy = broker_Proxy_conn.max_num_proxy;
    }

  pthread_mutex_unlock (&proxy_conn_mutex);
  return ret;
}

int
broker_delete_proxy_conn_by_fd (SOCKET fd)
{
  int ret = 0;
  T_PROXY_CONN_ENT *ent_p, *prev_ent_p;

  pthread_mutex_lock (&proxy_conn_mutex);
  for (prev_ent_p = ent_p = broker_Proxy_conn.proxy_conn_ent; ent_p; prev_ent_p = ent_p, ent_p = ent_p->next)
    {
      if (ent_p->fd == fd)
	{
	  if (ent_p == broker_Proxy_conn.proxy_conn_ent)
	    {
	      broker_Proxy_conn.proxy_conn_ent = ent_p->next;
	    }
	  else
	    {
	      prev_ent_p->next = ent_p->next;
	    }

	  broker_Proxy_conn.cur_num_proxy--;
	  if (broker_Proxy_conn.cur_num_proxy < 0)
	    {
	      assert (false);
	      broker_Proxy_conn.cur_num_proxy = 0;
	    }

	  FREE_MEM (ent_p);
	  ent_p = NULL;
	  break;
	}
    }
  pthread_mutex_unlock (&proxy_conn_mutex);

  return ret;
}

int
broker_delete_proxy_conn_by_proxy_id (int proxy_id)
{
  int ret = 0;
  T_PROXY_CONN_ENT *ent_p, *prev_ent_p;

  if (proxy_id == PROXY_INVALID_ID)
    {
      return -1;
    }

  pthread_mutex_lock (&proxy_conn_mutex);
  for (prev_ent_p = ent_p = broker_Proxy_conn.proxy_conn_ent; ent_p; prev_ent_p = ent_p, ent_p = ent_p->next)
    {
      if (ent_p->proxy_id == proxy_id)
	{
	  if (ent_p == broker_Proxy_conn.proxy_conn_ent)
	    {
	      broker_Proxy_conn.proxy_conn_ent = ent_p->next;
	    }
	  else
	    {
	      prev_ent_p->next = ent_p->next;
	    }

	  broker_Proxy_conn.cur_num_proxy--;
	  if (broker_Proxy_conn.cur_num_proxy < 0)
	    {
	      assert (false);
	      broker_Proxy_conn.cur_num_proxy = 0;
	    }

	  FREE_MEM (ent_p);
	  ent_p = NULL;
	  break;
	}
    }

  pthread_mutex_unlock (&proxy_conn_mutex);

  return ret;
}

int
broker_register_proxy_conn (SOCKET fd, int proxy_id)
{
  int ret = 0;
  T_PROXY_CONN_ENT *ent_p;

  pthread_mutex_lock (&proxy_conn_mutex);
  ent_p = broker_find_proxy_conn_by_fd (fd);
  if (ent_p == NULL)
    {
      pthread_mutex_unlock (&proxy_conn_mutex);
      return -1;
    }
  assert (ent_p->status != PROXY_CONN_AVAILABLE);
  assert (ent_p->proxy_id == PROXY_INVALID_ID);
  if (ent_p->status == PROXY_CONN_AVAILABLE || ent_p->proxy_id != PROXY_INVALID_ID)
    {
      pthread_mutex_unlock (&proxy_conn_mutex);
      return -1;
    }

  ent_p->status = PROXY_CONN_AVAILABLE;
  ent_p->proxy_id = proxy_id;

  pthread_mutex_unlock (&proxy_conn_mutex);

  return ret;
}
#endif /* !WINDOWS */

#if defined(WINDOWS)
int
broker_find_available_proxy (T_SHM_PROXY * shm_proxy_p, int ip_addr, T_BROKER_VERSION clt_version)
#else /* WINDOWS */
SOCKET
broker_find_available_proxy (T_SHM_PROXY * shm_proxy_p)
#endif				/* !WINDOWS */
{
  int proxy_index;
  int min_cur_client = -1;
  int cur_client = -1;
  int max_context = -1;
  T_PROXY_INFO *proxy_info_p;
#if defined(WINDOWS)
  T_PROXY_INFO *find_proxy_info_p;
#else /* WINDOWS */
  T_PROXY_CONN_ENT *ent_p;
  SOCKET fd = INVALID_SOCKET;
  int retry_count = 0;

  if (broker_Proxy_conn.max_num_proxy < 0)
    {
      return INVALID_SOCKET;
    }

retry:
  pthread_mutex_lock (&proxy_conn_mutex);
#endif /* !WINDOWS */
  for (proxy_index = 0; proxy_index < shm_proxy_p->num_proxy; proxy_index++)
    {
      proxy_info_p = shard_shm_find_proxy_info (shm_proxy_p, proxy_index);

      if (proxy_info_p->pid <= 0)
	{
	  continue;
	}

      max_context = proxy_info_p->max_context;
      cur_client = proxy_info_p->cur_client;

#if !defined(WINDOWS)
      ent_p = broker_find_proxy_conn_by_id (proxy_info_p->proxy_id);
      if (ent_p == NULL || ent_p->status != PROXY_CONN_AVAILABLE)
	{
	  continue;
	}

      assert (ent_p->fd != INVALID_SOCKET);
#endif /* !WINDOWS */

      if (min_cur_client == -1)
	{
	  min_cur_client = cur_client;
	}

      if (cur_client < max_context && cur_client <= min_cur_client)
	{
#if defined(WINDOWS)
	  find_proxy_info_p = proxy_info_p;
#else /* WINDOWS */
	  fd = ent_p->fd;
#endif /* !WINDOWS */

	  min_cur_client = cur_client;
	}
    }

    if (shm_proxy_p->num_proxy > 0 && fd < 0 && retry_count++ < PROXY_SVR_CON_RETRY_COUNT)
      {
	pthread_mutex_unlock (&proxy_conn_mutex);
	SLEEP_MILISEC (0, PROXY_SVR_CON_RETRY_MSEC);
	goto retry;
      }

#if !defined(WINDOWS)
  pthread_mutex_unlock (&proxy_conn_mutex);
#endif /* !WINDOWS */

#if defined(WINDOWS)
  return find_proxy_info_p->proxy_port;
#else /* WINDOWS */
  return fd;
#endif /* !WINDOWS */
}

#if !defined(WINDOWS)
SOCKET
broker_get_proxy_conn_maxfd (SOCKET proxy_sock_fd)
{
  T_PROXY_CONN_ENT *ent_p;
  int max_fd;

  max_fd = proxy_sock_fd;

  pthread_mutex_lock (&proxy_conn_mutex);
  for (ent_p = broker_Proxy_conn.proxy_conn_ent; ent_p; ent_p = ent_p->next)
    {
      if (ent_p->status != PROXY_CONN_NOT_CONNECTED)
	{
	  if (max_fd < ent_p->fd)
	    {
	      max_fd = ent_p->fd;
	    }
	}
    }
  pthread_mutex_unlock (&proxy_conn_mutex);

  return (max_fd + 1);
}

int
broker_init_proxy_conn (int max_proxy)
{
  if (broker_Proxy_conn.max_num_proxy >= 0)
    {
      return 0;
    }

  pthread_mutex_init (&proxy_conn_mutex, NULL);

  broker_Proxy_conn.max_num_proxy = max_proxy;
  broker_Proxy_conn.cur_num_proxy = 0;
  broker_Proxy_conn.proxy_conn_ent = NULL;

  return 0;
}

void
broker_destroy_proxy_conn (void)
{
  if (broker_Proxy_conn.max_num_proxy < 0)
    {
      return;
    }

  pthread_mutex_lock (&proxy_conn_mutex);
  broker_Proxy_conn.max_num_proxy = -1;
  broker_Proxy_conn.cur_num_proxy = 0;

  broker_free_all_proxy_conn_ent ();
  pthread_mutex_unlock (&proxy_conn_mutex);

  pthread_mutex_destroy (&proxy_conn_mutex);

  return;
}
#endif /* !WINDOWS */
