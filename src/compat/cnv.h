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
 * cnv.h - String conversion function header
 */

#ifndef _CNV_H_
#define _CNV_H_

#ident "$Id$"

#include "dbtype_def.h"

extern int db_bit_string (const DB_VALUE * the_db_bit, const char *bit_format, char *string, int max_size);

#endif /* _CNV_H_ */
