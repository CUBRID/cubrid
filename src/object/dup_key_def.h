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
 * dup_key_def.h - Definition for management of duplicate key indexes
 */

#ifndef _DUP_KEY_DEF_H_
#define _DUP_KEY_DEF_H_


// This file is a temporary file.
// Once the code is applied, we will remove it.

#define SUPPORT_KEY_DUP_LEVEL

#if defined(SUPPORT_KEY_DUP_LEVEL)
// view(db_index, db_index_key), show index statement
//#define ENABLE_SHOW_HIDDEN_ATTR

#define FAKE_RESERVED_INDEX_ATTR

//#define SUPPORT_KEY_DUP_LEVEL_FK
#endif

#endif // _DUP_KEY_DEF_H_
