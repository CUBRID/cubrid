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
 * cas_db_inc.h -
 */

#ifndef	_CAS_DB_INC_H_
#define	_CAS_DB_INC_H_

#ident "$Id$"

#include "dbi.h"
#include "dbtype_def.h"
/*#include "db.h"*/
extern int db_get_connect_status (void);
extern void db_set_connect_status (int status);

#define CUBRID_VERSION(X, Y)	(((X) << 8) | (Y))
#define CUR_CUBRID_VERSION	\
	CUBRID_VERSION(MAJOR_VERSION, MINOR_VERSION)

#if defined(WINDOWS)
extern char **db_get_lock_classes (DB_SESSION * session);
#endif

#endif /* _CAS_DB_INC_H_ */
