/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * connless.h - 
 */

#ifndef _CONNLESS_H_
#define _CONNLESS_H_

#ident "$Id$"

#include "defs.h"

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
#endif /* _CONNLESS_H_ */
