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
 * Transaction isolation definitions
 */

#ifndef _DBTRAN_DEF_H_
#define _DBTRAN_DEF_H_

typedef enum
{
  TRAN_UNKNOWN_ISOLATION = 0x00,	/* 0 0000 */

  TRAN_READ_COMMITTED = 0x04,	/* 0 0100 */
  TRAN_REP_CLASS_COMMIT_INSTANCE = 0x04,	/* Alias of above */
  TRAN_CURSOR_STABILITY = 0x04,	/* Alias of above */

  TRAN_REPEATABLE_READ = 0x05,	/* 0 0101 */
  TRAN_REP_READ = 0x05,		/* Alias of above */
  TRAN_REP_CLASS_REP_INSTANCE = 0x05,	/* Alias of above */
  TRAN_DEGREE_2_9999_CONSISTENCY = 0x05,	/* Alias of above */

  TRAN_SERIALIZABLE = 0x06,	/* 0 0110 */
  TRAN_DEGREE_3_CONSISTENCY = 0x06,	/* Alias of above */
  TRAN_NO_PHANTOM_READ = 0x06,	/* Alias of above */

  TRAN_DEFAULT_ISOLATION = TRAN_READ_COMMITTED,
  MVCC_TRAN_DEFAULT_ISOLATION = TRAN_READ_COMMITTED,

  TRAN_MINVALUE_ISOLATION = 0x04,	/* internal use only */
  TRAN_MAXVALUE_ISOLATION = 0x06,	/* internal use only */

  // aliases for CCI compatibility
  TRAN_ISOLATION_MIN = 0x04,
  TRAN_ISOLATION_MAX = 0x06,
} DB_TRAN_ISOLATION;

#define IS_VALID_ISOLATION_LEVEL(isolation_level) \
    (TRAN_MINVALUE_ISOLATION <= (isolation_level) \
     && (isolation_level) <= TRAN_MAXVALUE_ISOLATION)

#define TRAN_DEFAULT_ISOLATION_LEVEL()	(TRAN_DEFAULT_ISOLATION)

#define TRAN_ASYNC_WS_BIT                        0x10	/* 1 0000 */
#define TRAN_ISO_LVL_BITS                        0x0F	/* 0 1111 */

#endif // _DBTRAN_DEF_H_
