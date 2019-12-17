/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
