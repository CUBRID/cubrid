/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * db_inc.h -
 */

#ifndef	_DB_INC_H_
#define	_DB_INC_H_

#ident "$Id$"

#include "dbi.h"
/*#include "db.h"*/
extern int db_Connect_status;

#ifndef WIN32
/* this must be the last header file included!!! */
#include "dbval.h"
#endif

#define CUBRID_VERSION(X, Y)	(((X) << 8) | (Y))
#define CUR_CUBRID_VERSION	\
	CUBRID_VERSION(MAJOR_VERSION, MINOR_VERSION)

#ifdef WIN32
extern char **db_get_lock_classes (DB_SESSION * session);
#endif

extern void histo_clear (void);
extern void histo_print (void);


#endif /* _DB_INC_H_ */
