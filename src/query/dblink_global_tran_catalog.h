/*
 *
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
 * dblink_global_tran_catalog.h - locator API for _db_global_tran insert/update/delete/scan
 */

#ifndef _DBLINK_GLOBAL_TRAN_CATALOG_H_
#define _DBLINK_GLOBAL_TRAN_CATALOG_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif

#include "thread_compat.hpp"

#ifdef CCI_XA

/* Row data for _db_global_tran (for recovery callback) */
typedef struct dblink_global_tran_row DBLINK_GLOBAL_TRAN_ROW;
struct dblink_global_tran_row
{
  int gtrid;
  int bqual;
  char conn_url[MAX_LEN_CONNECTION_URL + 1];
  char user_name[DB_MAX_USER_LENGTH + 1];
  char password[DB_MAX_PASSWORD_LENGTH + 1];
  char state;			/* 'P', 'A', 'C' */
};

/*
 * Insert one row into _db_global_tran (state 'P' before prepare).
 * Returns NO_ERROR on success.
 */
extern int dblink_global_tran_insert_row (THREAD_ENTRY * thread_p, int gtrid, int bqual,
					  const char *conn_url, const char *user_name, const char *password,
					  char state);

/*
 * Update state (and updated_date) for the row with (gtrid, bqual).
 * Returns NO_ERROR on success, ER_* if row not found or error.
 */
extern int dblink_global_tran_update_state (THREAD_ENTRY * thread_p, int gtrid, int bqual, char new_state);

/*
 * Delete the row with (gtrid, bqual). Returns NO_ERROR on success.
 */
extern int dblink_global_tran_delete_row (THREAD_ENTRY * thread_p, int gtrid, int bqual);

/*
 * Callback for scan: return true to continue, false to stop.
 * row_data is valid only during the callback.
 */
typedef bool (*dblink_global_tran_scan_callback) (const DBLINK_GLOBAL_TRAN_ROW * row_data);

/*
 * Scan _db_global_tran rows where state is 'P', 'A', or 'C' (for recovery).
 * Invokes callback for each matching row. row_oid can be used for delete after send decision.
 */
extern int dblink_global_tran_scan_for_recovery (THREAD_ENTRY * thread_p, dblink_global_tran_scan_callback callback);

#endif /* CCI_XA */

#endif /* _DBLINK_GLOBAL_TRAN_CATALOG_H_ */
