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
 * csql_sql_log.h - register the CSQL sink for SP-issued SQL logging
 */

#ifndef _CSQL_SQL_LOG_H_
#define _CSQL_SQL_LOG_H_

#ident "$Id$"

/*
 * Register the CSQL writer so that SQL issued from stored procedures is written
 * into csql.err (gated by the csql_log_sql_from_sp system parameter, default on).
 * Must be called once during CSQL startup, after the error manager is initialized.
 */
extern void csql_sql_log_sp_init (void);

#endif /* _CSQL_SQL_LOG_H_ */
