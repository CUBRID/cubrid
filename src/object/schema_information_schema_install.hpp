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
 * schema_information_schema_install.hpp
 */

#ifndef _SCHEMA_INFORMATION_SCHEMA_INSTALL_HPP_
#define _SCHEMA_INFORMATION_SCHEMA_INSTALL_HPP_

#include "porting.h"

#define INFO_SCHEMA_COLUMNS                     "INFORMATION_SCHEMA.columns"
#define INFO_SCHEMA_DOMAINS                     "INFORMATION_SCHEMA.domains"
#define INFO_SCHEMA_KEY_COLUMN_USAGE            "INFORMATION_SCHEMA.key_column_usage"
#define INFO_SCHEMA_PARAMETERS                  "INFORMATION_SCHEMA.parameters"
#define INFO_SCHEMA_PARTITIONS                  "INFORMATION_SCHEMA.partitions"
#define INFO_SCHEMA_REFERENTIAL_CONSTRAINTS     "INFORMATION_SCHEMA.referential_constraints"
#define INFO_SCHEMA_ROUTINES                    "INFORMATION_SCHEMA.routines"
#define INFO_SCHEMA_SCHEMATA                    "INFORMATION_SCHEMA.schemata"
#define INFO_SCHEMA_SEQUENCES                   "INFORMATION_SCHEMA.sequences"
#define INFO_SCHEMA_SYNONYMS                    "INFORMATION_SCHEMA.synonyms"
#define INFO_SCHEMA_TABLE_CONSTRAINTS           "INFORMATION_SCHEMA.table_constraints"
#define INFO_SCHEMA_TABLE_PRIVILEGES            "INFORMATION_SCHEMA.table_privileges"
#define INFO_SCHEMA_TABLES                      "INFORMATION_SCHEMA.tables"
#define INFO_SCHEMA_TRIGGERS                    "INFORMATION_SCHEMA.triggers"
#define INFO_SCHEMA_VIEWS                       "INFORMATION_SCHEMA.views"

extern EXPORT_IMPORT void info_schema_init (void);
extern EXPORT_IMPORT int info_schema_install (void);

#endif /* _SCHEMA_INFORMATION_SCHEMA_INSTALL_HPP_ */
