/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * server.h - Server interface functions
 *
 * Note: This file defines the interface to the server. The client modules that
 * calls any funciton in the server should include this module instead of the
 * header file of the desired function.
 */

#ifndef _SERVER_H_
#define _SERVER_H_

#ident "$Id$"

enum
{ SI_SYS_TIMESTAMP = 1, SI_LOCAL_TRANSACTION_ID = 2,	/* next is 4 */
  SI_CNT = 2
};

enum
{
  CHECKDB_FILE_TRACKER_CHECK = 1,
  CHECKDB_HEAP_CHECK_ALLHEAPS = 2,
  CHECKDB_CT_CHECK_CAT_CONSISTENCY = 4,
  CHECKDB_BTREE_CHECK_ALL_BTREES = 8,
  CHECKDB_LC_CHECK_CLASSNAMES = 16,
  CHECKDB_LC_CHECK_ALLENTRIES_OF_ALLBTREES = 32,
  CHECKDB_REPAIR = 64
};

#define CHECKDB_ALL_CHECK \
    (CHECKDB_FILE_TRACKER_CHECK         | CHECKDB_HEAP_CHECK_ALLHEAPS  | \
     CHECKDB_CT_CHECK_CAT_CONSISTENCY | CHECKDB_BTREE_CHECK_ALL_BTREES  | \
     CHECKDB_LC_CHECK_CLASSNAMES      | CHECKDB_LC_CHECK_ALLENTRIES_OF_ALLBTREES)

extern unsigned int db_on_server;

#endif /* _SERVER_H_ */
