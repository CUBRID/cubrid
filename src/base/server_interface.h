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
 * server_interface.h - Server interface functions
 *
 * Note: This file defines the interface to the server. The client modules that
 * calls any function in the server should include this module instead of the
 * header file of the desired function.
 */

#ifndef _SERVER_INTERFACE_H_
#define _SERVER_INTERFACE_H_

#ident "$Id$"

enum
{
  SI_SYS_DATETIME = 0x01,
  SI_LOCAL_TRANSACTION_ID = 0x02
    /* next is 0x04 */
};

enum
{
  CHECKDB_FILE_TRACKER_CHECK = 1,
  CHECKDB_HEAP_CHECK_ALLHEAPS = 2,
  CHECKDB_CT_CHECK_CAT_CONSISTENCY = 4,
  CHECKDB_BTREE_CHECK_ALL_BTREES = 8,
  CHECKDB_LC_CHECK_CLASSNAMES = 16,
  CHECKDB_LC_CHECK_ALLENTRIES_OF_ALLBTREES = 32,
  CHECKDB_REPAIR = 64,
  CHECKDB_CHECK_PREV_LINK = 128,
  CHECKDB_REPAIR_PREV_LINK = 256
};

#define CHECKDB_ALL_CHECK_EXCEPT_PREV_LINK \
    (CHECKDB_FILE_TRACKER_CHECK         | CHECKDB_HEAP_CHECK_ALLHEAPS  | \
     CHECKDB_CT_CHECK_CAT_CONSISTENCY | CHECKDB_BTREE_CHECK_ALL_BTREES  | \
     CHECKDB_LC_CHECK_CLASSNAMES      | CHECKDB_LC_CHECK_ALLENTRIES_OF_ALLBTREES)

#define COMPACTDB_LOCKED_CLASS -1
#define COMPACTDB_INVALID_CLASS -2
#define COMPACTDB_UNPROCESSED_CLASS -3

#define COMPACTDB_REPR_DELETED -2

enum
{ GENERATE_SERIAL = 0, GENERATE_AUTO_INCREMENT = 1 };

#endif /* _SERVER_INTERFACE_H_ */
