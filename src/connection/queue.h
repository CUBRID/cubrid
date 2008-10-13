/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * queue.h - 
 */

#ifndef _QUEUE_H_
#define _QUEUE_H_

#ident "$Id$"

#include "defs.h"

extern char *css_return_data_buffer (CSS_CONN_ENTRY * conn,
				     unsigned short request_id,
				     int *buffer_size);
extern char *css_return_oob_buffer (int *buffer_size);
extern int css_return_queued_data (CSS_CONN_ENTRY * conn,
				   unsigned short request_id, char **buffer,
				   int *buffer_size, int *rc);
extern int css_return_queued_error (CSS_CONN_ENTRY * conn,
				    unsigned short request_id, char **buffer,
				    int *buffer_size, int *rc);
extern int css_return_queued_request (CSS_CONN_ENTRY * conn,
				      unsigned short *rid, int *request,
				      int *buffer_size);
extern int css_return_queued_oob (CSS_CONN_ENTRY * conn, char **buffer,
				  int *buffer_size);
extern bool css_test_for_queued_request (CSS_CONN_ENTRY * conn);
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
extern void css_remove_all_unexpected_packets (CSS_CONN_ENTRY * conn);
extern int css_queue_user_data_buffer (CSS_CONN_ENTRY * conn,
				       unsigned short request_id, int size,
				       char *buffer);
extern CSS_QUEUE_ENTRY *css_find_queue_entry (CSS_QUEUE_ENTRY * header,
					      unsigned int key);

#endif /* _QUEUE_H_ */
