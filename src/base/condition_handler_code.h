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
 * condition_handler_code.h : condition code definitions
 *
 */

#ifndef _CONDITION_HANDLER_CODE_H_
#define _CONDITION_HANDLER_CODE_H_

#ident "$Id$"

/* Constants for encoding and decoding CO_CODE values. */
#define CO_MAX_CODE         1024	/* Max codes per module */
#define CO_MAX_MODULE       INT_MAX/CO_MAX_CODE	/* Max module identifier */

#define CO_CODE(MODULE, CODE)           \
  -((int)MODULE * (int)CO_MAX_CODE + (int)CODE - (int)1)

/* co module names */
typedef enum
{
  CO_MODULE_CO = 1,
  CO_MODULE_MTS = 2,
  CO_MODULE_SYS = 13,
  CO_MODULE_CNV = 27,
  CO_MODULE_ARGS = 1000
} CO_MODULE;

#endif /* _CONDITION_HANDLER_CODE_H_ */
