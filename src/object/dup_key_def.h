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

//#define SUPPORT_KEY_DUP_LEVEL_IGNORE_MODE_LEVEL
#define KEEP_REBUILD_POLICY
#define SUPPORT_KEY_DUP_LEVEL_TEST_FK_NAME

#define SUPPORT_KEY_DUP_LEVEL_FK
#endif

/* TODO:
 * 1. heap_attrvalue_get_key, heap_midxkey_key_get 에서 파라메터 제거하고 <match type> 점검에 대한 공통루틴 이용할까?
 * 2.  btree_check_foreign_key에서도 점검해야 할지 조사 필요 --> check_fk_validity()에서 조치를 하고 들어옴
*/
#endif // _DUP_KEY_DEF_H_
