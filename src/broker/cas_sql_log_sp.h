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
 * cas_sql_log_sp.h - register the CAS sink for SP-issued SQL logging
 */

#ifndef _CAS_SQL_LOG_SP_H_
#define _CAS_SQL_LOG_SP_H_

#ident "$Id$"

/*
 * Register the CAS writer so that SQL issued from stored procedures is written
 * into this CAS process' *.sql.log (gated by the broker SQL_LOG parameter).
 * Must be called once during CAS startup.
 */
extern void cas_sql_log_sp_init (void);

#endif /* _CAS_SQL_LOG_SP_H_ */
