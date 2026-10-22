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

#ifndef _INTERNAL_LOB_DML_PROTOCOL_HPP_
#define _INTERNAL_LOB_DML_PROTOCOL_HPP_

#include "dbtype_def.h"
#include "object_representation.h"

#define INTERNAL_LOB_DML_CONFIG_VERSION 2

#define INTERNAL_LOB_DML_SLOT_FLAG_DIRECT_REVERSE 0x01
#define INTERNAL_LOB_DML_SLOT_CONFIG_SIZE (OR_INT_SIZE * 2 + OR_INT64_SIZE * 2 + OR_OID_SIZE)

struct internal_lob_dml_slot_config
{
  DB_TYPE type = DB_TYPE_NULL;
  DB_BIGINT data_length = -1;
  DB_BIGINT logical_length = -1;
  int flags = 0;
  OID class_oid = OID_INITIALIZER;
};

#endif /* _INTERNAL_LOB_DML_PROTOCOL_HPP_ */
