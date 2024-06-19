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

#include <cstddef>
#include <cstring>
#include <cassert>
#include <dbtype_def.h>

/* CAUTION: the following list must match DB_TYPE defined in dbtype_def.h */
static const char *db_type_names[DB_TYPE_LAST + 1] =
{
    NULL /* NULL */,
    "INTEGER",
    "FLOAT",
    "DOUBLE",
    "VARCHAR",
    "OBJECT",
    "SET",
    "MULTISET",
    "SEQUENCE",
    "ELO",
    "TIME",
    "TIMESTAMP",
    "DATE",
    "MONETARY",
    NULL /* VARIABLE */,
    NULL /* SUB */,
    NULL /* POINTER */,
    NULL /* ERROR */,
    "SHORT",
    NULL /* VOBJ */,
    NULL /* OID */,
    NULL /* DB_VALUE */,
    "NUMERIC",
    "BIT",
    "VARBIT",
    "CHAR",
    "NCHAR",
    "VARNCHAR",
    NULL /* RESULTSET */,
    NULL /* MIDXKEY */,
    NULL /* TABLE */,
    "BIGINT",
    "DATETIME",
    "BLOB",
    "CLOB",
    "ENUM",
    "TIMESTAMPTZ",
    "TIMESTAMPLTZ",
    "DATETIMETZ",
    "DATETIMELTZ",
    "JSON"
};

int
db_get_db_type_of_name(const char *name)
{
    assert (name);

    int i;
    for (i = 0; i <= DB_TYPE_LAST; i++) {
        const char *name_i = db_type_names[i];
        if (name_i && strcmp(name, name_i) == 0) {
            return i;
        }
    }

    return -1;  // no such DB_TYPE
}

const char*
db_get_name_of_db_type(const int db_type)
{
    if (db_type >= 0 && db_type <= DB_TYPE_LAST) {
        return db_type_names[db_type];
    } else {
        return NULL;
    }
}
