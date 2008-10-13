/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * oob.h - interface definitions for the out of band message handling routines
 */

#ifndef _OOB_H_
#define _OOB_H_

#ident "$Id$"

/*
 * The message types that can be used with Out Of Band Messages
 * if adding more types, always add BEFORE OOB_LAST_ENUM.
 */
typedef enum oob_types
{
  OOB_SERVER_SHUTDOWN = 0,	/* Notification from server: going down       */
  OOB_SERVER_DOWN,		/* Server is now down, close socket on client */
  OOB_CLIENT_TO_SERVER,		/* Sent from XSQL client to XSQL server       */
  OOB_WAITING_POLL,		/* Sent from server to client                 */
  OOB_LAST_ENUM			/* MUST BE LAST (defines number of enums)     */
} OOB_ENUM_TYPE;

#if !defined(SERVER_MODE)
typedef void (*OOB_HANDLER_FUNCTION) (unsigned int eid);

extern void css_init_oob_handler (void);
extern void css_set_oob_handler (OOB_ENUM_TYPE oob_type,
				 OOB_HANDLER_FUNCTION handler);
#endif /* !SERVER_MODE */

#endif /* _OOB_H_ */
