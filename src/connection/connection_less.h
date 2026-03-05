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
 * connection_less.h -
 */

#ifndef _CONNECTION_LESS_H_
#define _CONNECTION_LESS_H_

#ident "$Id$"

#include "connection_defs.h"

class connection_less
{
private:
  unsigned short m_entry_id;
  CSS_MAP_ENTRY *m_css_map_entry;
#if defined(MULTI_CONN_TO_A_SERVER)
  pthread_mutex_t m_css_map_entry_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

private:
  unsigned short css_make_entry_id ();
  CSS_MAP_ENTRY *css_get_queued_entry (char *host);
  int css_test_for_open_conn (CSS_CONN_ENTRY * conn);

public:
    connection_less ();
   ~connection_less () = default;

  unsigned int css_make_eid (unsigned short host_id, unsigned short rid);
  CSS_MAP_ENTRY *css_return_entry_from_eid (unsigned int eid);
  CSS_MAP_ENTRY *css_queue_connection (CSS_CONN_ENTRY * conn, const char *host);

  void css_remove_queued_connection_by_entry (CSS_MAP_ENTRY * entry);

  CSS_MAP_ENTRY *css_return_open_entry (char *host);
  CSS_MAP_ENTRY *css_get_map_entry ();

#if defined(UNUSED_FUNCTION)
  CSS_MAP_ENTRY *css_return_entry_from_conn (CSS_CONN_ENTRY * conn);
  unsigned int css_return_eid_from_conn (CSS_CONN_ENTRY * conn, unsigned short rid);
#endif
};
#endif /* _CONNECTION_LESS_H_ */
