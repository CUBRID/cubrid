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
 * broker_proxy_conn.c -
 */

#ident "$Id$"

#if defined(CUBRID_SHARD)
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

#include "porting.h"
#include "broker_proxy_conn.h"
#include "shard_proxy_common.h"
#include "shard_shm.h"

T_PROXY_CONN broker_Proxy_conn = {
  -1,				/* max_num_proxy */
  0,				/* cur_num_proxy */
  NULL				/* proxy_sockfd */
};

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
      if (ent_p->fd != INVALID_SOCKET
	  && ent_p->status == PROXY_CONN_CONNECTED)
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
  for (prev_ent_p = ent_p = broker_Proxy_conn.proxy_conn_ent;
       ent_p; prev_ent_p = ent_p, ent_p = ent_p->next)
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
  for (prev_ent_p = ent_p = broker_Proxy_conn.proxy_conn_ent;
       ent_p; prev_ent_p = ent_p, ent_p = ent_p->next)
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
  int ret;
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
  if (ent_p->status == PROXY_CONN_AVAILABLE
      || ent_p->proxy_id != PROXY_INVALID_ID)
    {
      pthread_mutex_unlock (&proxy_conn_mutex);
      return -1;
    }

  ent_p->status = PROXY_CONN_AVAILABLE;
  ent_p->proxy_id = proxy_id;

  pthread_mutex_unlock (&proxy_conn_mutex);

  return ret;
}

SOCKET
broker_find_available_proxy (T_SHM_PROXY * shm_proxy_p)
{
  int i;
  SOCKET fd = INVALID_SOCKET;
  int min_cur_client = -1;
  int cur_client = -1;
  int max_client = -1;
  T_PROXY_INFO *proxy_info_p;
  T_PROXY_CONN_ENT *ent_p;

  if (broker_Proxy_conn.max_num_proxy < 0)
    {
      return INVALID_SOCKET;
    }

  pthread_mutex_lock (&proxy_conn_mutex);
  for (proxy_info_p = shard_shm_get_first_proxy_info (shm_proxy_p);
       proxy_info_p;
       proxy_info_p = shard_shm_get_next_proxy_info (proxy_info_p))
    {
      if (proxy_info_p->pid <= 0)
	{
	  continue;
	}

      max_client = proxy_info_p->max_client - 1;
      cur_client = proxy_info_p->cur_client;

      ent_p = broker_find_proxy_conn_by_id (proxy_info_p->proxy_id);
      if (ent_p == NULL || ent_p->status != PROXY_CONN_AVAILABLE)
	{
	  continue;
	}

      assert (ent_p->fd != INVALID_SOCKET);

      if (min_cur_client == -1)
	{
	  min_cur_client = cur_client;
	}

      if (cur_client < max_client && cur_client <= min_cur_client)
	{
	  fd = ent_p->fd;
	  min_cur_client = cur_client;
	}
    }
  pthread_mutex_unlock (&proxy_conn_mutex);

  return fd;
}

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

#endif /* CUBRID_SHARD */
