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

//
// Identifiers for log volumes (and others...)
//

#ifndef _LOG_VOLIDS_HPP_
#define _LOG_VOLIDS_HPP_

#include "storage_common.h"

/*
 * NOTE: NULL_VOLID generally means a bad volume identifier
 *       Negative volume identifiers are used to identify auxiliary files and
 *       volumes (e.g., logs, backups)
 */

const VOLID LOG_MAX_DBVOLID = VOLID_MAX - 1;

/* Volid of database.txt */
const VOLID LOG_DBTXT_VOLID = SHRT_MIN + 1;
const VOLID LOG_DBFIRST_VOLID = 0;

/* Volid of Transprent Data Encryption Keys (TDE Master keys) */
const VOLID LOG_DBTDE_KEYS_VOLID = LOG_DBFIRST_VOLID - 6;
/* Volid of volume information */
const VOLID LOG_DBVOLINFO_VOLID = LOG_DBFIRST_VOLID - 5;
/* Volid of info log */
const VOLID LOG_DBLOG_INFO_VOLID = LOG_DBFIRST_VOLID - 4;
/* Volid of backup info log */
const VOLID LOG_DBLOG_BKUPINFO_VOLID = LOG_DBFIRST_VOLID - 3;
/* Volid of active log */
const VOLID LOG_DBLOG_ACTIVE_VOLID = LOG_DBFIRST_VOLID - 2;
/* Volid of background archive logs */
const VOLID LOG_DBLOG_BG_ARCHIVE_VOLID = LOG_DBFIRST_VOLID - 21;
/* Volid of archive logs */
const VOLID LOG_DBLOG_ARCHIVE_VOLID = LOG_DBFIRST_VOLID - 20;
/* Volid of copies */
const VOLID LOG_DBCOPY_VOLID = LOG_DBFIRST_VOLID - 19;
/* Volid of double write buffer */
const VOLID LOG_DBDWB_VOLID = LOG_DBFIRST_VOLID - 22;

#endif // !_LOG_VOLIDS_HPP_
