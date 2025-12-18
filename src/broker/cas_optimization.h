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
 * cas_optimization.h -
 * Optimization level management functions
 * Used for query plan dumping and SQL logging
 */

#ifndef	_CAS_OPTIMIZATION_H_
#define	_CAS_OPTIMIZATION_H_

#ident "$Id$"

/*****************************
  Optimization level management functions
  moved from cas_sql_log2.c/h to separate file for better organization
  *****************************/

#define CHK_OPT_LEVEL(level)                ((level) & 0xff)
#define CHK_OPTIMIZATION_ENABLED(level)     (CHK_OPT_LEVEL(level) != 0)
#define CHK_PLAN_DUMP_ENABLED(level)        ((level) >= 0x100)
#define CHK_OPTIMIZATION_LEVEL_VALID(level) \
	  (CHK_OPTIMIZATION_ENABLED(level) \
	   || CHK_PLAN_DUMP_ENABLED(level) \
           || (level == 0))

extern void set_optimization_level (int level);
extern void reset_optimization_level_as_saved (void);

#endif /* _CAS_OPTIMIZATION_H_ */
