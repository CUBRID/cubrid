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
 * connection_less.cpp - "connectionless" interface for the client and server
 */

#ident "$Id$"

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "connection_less.h"

#if defined(SERVER_MODE)
#error Does not belong to server module
#endif

connection_less::connection_less ()
{
  m_entry_id = 0;
  m_css_map_entry = NULL;
}

/*
 * css_make_eid() - create an eid which is a combination of the entry id and
 *                  the request id
 *   return: enquiry id
 *   entry_id(in): entry id
 *   rid(in): request id
 */
unsigned int
connection_less::css_make_eid (unsigned short entry_id, unsigned short rid)
{
  int top;

  top = entry_id;
  return ((top << 16) | rid);
}

/*
 * css_return_entry_from_eid() - lookup a queue entry based on the entry id
 *   return: map entry if find, or NULL
 *   eid(in): enquiry id
 *   anchor(in): map entry anchor
 */
CSS_MAP_ENTRY *
connection_less::css_return_entry_from_eid (unsigned int eid)
{
  CSS_MAP_ENTRY *map_entry_p;
  unsigned short entry_id;

  entry_id = CSS_ENTRYID_FROM_EID (eid);
  for (map_entry_p = m_css_map_entry; map_entry_p; map_entry_p = map_entry_p->next)
    {
      if (map_entry_p->id == entry_id)
	{
	  return (map_entry_p);
	}
    }
  return (NULL);
}

/*
 * css_make_entry_id() - create an entry structure that will be queued for
 *                       reuse
 *   return: entry id
 */
unsigned short
connection_less::css_make_entry_id ()
{
  CSS_MAP_ENTRY *map_entry_p;
  unsigned short old_value;

  old_value = m_entry_id++;
  if (!m_entry_id)
    {
      m_entry_id++;
    }

  for (map_entry_p = m_css_map_entry; map_entry_p; map_entry_p = map_entry_p->next)
    {
      if (m_entry_id == old_value)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ERR_CSS_ENTRY_OVERRUN, 0);
	}

      if (m_entry_id == map_entry_p->id)
	{
	  m_entry_id++;
	  map_entry_p = m_css_map_entry;
	}
    }

  return m_entry_id;
}

/*
 * css_queue_connection() - connection onto the connection entry queue
 *   return: man entry if success, or NULL
 *   conn(in): connection
 *   host(in): host name to connec
 *   anchor(out): map entry anchor
 */
CSS_MAP_ENTRY *
connection_less::css_queue_connection (CSS_CONN_ENTRY *conn, const char *host)
{
  CSS_MAP_ENTRY *map_entry_p;

  assert (host != NULL);
  if (conn == NULL || host == NULL)
    {
      return NULL;
    }

  map_entry_p = (CSS_MAP_ENTRY *) malloc (sizeof (CSS_MAP_ENTRY));
  if (map_entry_p != NULL)
    {
      map_entry_p->key = (char *) malloc (strlen (host) + 1);
      if (map_entry_p->key != NULL)
	{
	  strcpy (map_entry_p->key, host);
	}
      else
	{
	  free (map_entry_p);
	  return (NULL);
	}

      map_entry_p->conn = conn;
      map_entry_p->next = m_css_map_entry;
      map_entry_p->id = css_make_entry_id ();
      m_css_map_entry = map_entry_p;

      return (map_entry_p);
    }

  return (NULL);
}

/*
 * css_get_queued_entry() - lookup a queue entry that has the same "name" as the
 *                          destination
 *   return: map entry if found, or NULL
 *   host(in): host name to find
 */
CSS_MAP_ENTRY *
connection_less::css_get_queued_entry (char *host)
{
  CSS_MAP_ENTRY *map_entry_p;

  for (map_entry_p = m_css_map_entry; map_entry_p; map_entry_p = map_entry_p->next)
    {
      if (strcmp (host, map_entry_p->key) == 0)
	{
	  return (map_entry_p);
	}
    }

  return (NULL);
}

/*
 * css_remove_queued_connection_by_entry() - remove the entry from our queue
 *                                           when a connection is "closed"
 *   return: void
 *   entry(in): entry to find
 *   anchor(in/out): map entry anchor
 */
void
connection_less::css_remove_queued_connection_by_entry (CSS_MAP_ENTRY *entry)
{
  CSS_MAP_ENTRY *map_entry_p, *prev_map_entry_p;

  for (map_entry_p = m_css_map_entry, prev_map_entry_p = NULL; map_entry_p;
       prev_map_entry_p = map_entry_p, map_entry_p = map_entry_p->next)
    {
      if (entry == map_entry_p)
	{
	  if (map_entry_p == m_css_map_entry)
	    {
	      m_css_map_entry = map_entry_p->next;
	    }
	  else
	    {
	      prev_map_entry_p->next = map_entry_p->next;
	    }
	  break;
	}
    }

  if (map_entry_p)
    {
      free_and_init (map_entry_p->key);
      free_and_init (map_entry_p);
    }
}

/*
 * css_test_for_open_conn () - test to see if the connection is still open
 *   return:
 *   conn(in):
 */
int
connection_less::css_test_for_open_conn (CSS_CONN_ENTRY *conn)
{
  return (conn && conn->status == CONN_OPEN);
}

/*
 * css_return_open_entry() - make sure that an open entry is returned
 *   return: map entry if open, or NULL
 *   host(in): host name to open
 *   anchor(in/out): map entry anchor
 *
 * Note: It does this by looking for a connection currently open to the host.
 *       If one is found, it is tested to be sure it is still open. If it is
 *       not open, or a connection is not found, a new connection is created
 *       and returned.
 */
CSS_MAP_ENTRY *
connection_less::css_return_open_entry (char *host)
{
  CSS_MAP_ENTRY *map_entry_p;

  map_entry_p = css_get_queued_entry (host);
  if (map_entry_p != NULL)
    {
      if (css_test_for_open_conn (map_entry_p->conn))
	{
	  return (map_entry_p);
	}
    }

  return (NULL);
}

CSS_MAP_ENTRY *
connection_less::css_get_map_entry()
{
  return m_css_map_entry;
}

#if defined(UNUSED_FUNCTION)
/*
 * css_return_entry_from_conn() - check the queue based on a conn_ptr
 *   return: the entry if it exists, or NULL
 *   conn(in): connection
 */
CSS_MAP_ENTRY *
connection_less::css_return_entry_from_conn (CSS_CONN_ENTRY *conn)
{
  CSS_MAP_ENTRY *map_entry_p;

  for (map_entry_p = m_css_map_entry; map_entry_p; map_entry_p = map_entry_p->next)
    {
      if (map_entry_p->conn == conn)
	{
	  return (map_entry_p);
	}
    }

  return (NULL);
}

/*
 * css_return_eid_from_conn() - return an eid from a conn pointer
 *   return: enquiry id
 *   conn(in): connection
 *   anchor(in/out):  map entry anchor
 *   rid(in): request id
 *
 * Note: If the conn is not queued, it will be added, and the eid computed.
 *       This is for use by servers ONLY (note lack of host name).
 */
unsigned int
connection_less::css_return_eid_from_conn (CSS_CONN_ENTRY *conn, unsigned short rid)
{
  CSS_MAP_ENTRY *map_entry_p;

  map_entry_p = css_return_entry_from_conn (conn);
  if (map_entry_p == NULL)
    {
      map_entry_p = css_queue_connection (conn, (char *) "");
    }

  if (map_entry_p == NULL)
    {
      return 0;
    }
  else
    {
      return (css_make_eid (map_entry_p->id, rid));
    }
}
#endif // #if defined(UNUSED_FUNCTION)
