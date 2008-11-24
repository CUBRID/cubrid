/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */


/*
 * connection_less.h -
 */

#ifndef _CONNECTION_LESS_H_
#define _CONNECTION_LESS_H_

#ident "$Id$"

#include "connection_defs.h"

extern unsigned int css_make_eid (unsigned short host_id, unsigned short rid);
extern unsigned short css_return_rid_from_eid (unsigned int eid);
extern CSS_MAP_ENTRY *css_queue_connection (CSS_CONN_ENTRY * conn,
					    const char *host,
					    CSS_MAP_ENTRY ** anchor);
extern CSS_MAP_ENTRY *css_return_entry_from_eid (unsigned int eid,
						 CSS_MAP_ENTRY * anchor);
extern void css_remove_queued_connection_by_entry (CSS_MAP_ENTRY * entry,
						   CSS_MAP_ENTRY ** anchor);
extern CSS_MAP_ENTRY *css_return_open_entry (char *host,
					     CSS_MAP_ENTRY ** anchor);
extern unsigned int css_return_eid_from_conn (CSS_CONN_ENTRY * conn,
					      CSS_MAP_ENTRY ** anchor,
					      unsigned short rid);
extern CSS_MAP_ENTRY *css_return_entry_from_conn (CSS_CONN_ENTRY * conn,
						  CSS_MAP_ENTRY * anchor);
#endif /* _CONNECTION_LESS_H_ */
