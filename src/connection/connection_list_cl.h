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
 * connection_list_cl.h -
 */

#ifndef _CONNECTION_LIST_CL_H_
#define _CONNECTION_LIST_CL_H_

#ident "$Id$"

#include "connection_defs.h"

extern void css_queue_unexpected_data_packet (CSS_CONN_ENTRY * conn,
					      unsigned short request_id,
					      char *header, int size, int rc);
extern void css_queue_unexpected_error_packet (CSS_CONN_ENTRY * conn,
					       unsigned short request_id,
					       char *header, int size,
					       int rc);
extern void css_queue_unexpected_packet (int type, CSS_CONN_ENTRY * conn,
					 unsigned short request_id,
					 NET_HEADER * header, int size);
extern int css_queue_user_data_buffer (CSS_CONN_ENTRY * conn,
				       unsigned short request_id, int size,
				       char *buffer);
extern CSS_QUEUE_ENTRY *css_find_queue_entry (CSS_QUEUE_ENTRY * header,
					      unsigned int key);
extern void css_queue_remove_header_entry_ptr (CSS_QUEUE_ENTRY ** anchor,
                                         CSS_QUEUE_ENTRY * entry);
extern void css_queue_remove_header_entry (CSS_QUEUE_ENTRY ** anchor,
                                     unsigned short request_id);
extern void css_queue_remove_header (CSS_QUEUE_ENTRY ** anchor);

#endif /* _CONNECTION_LIST_CL_H_ */
