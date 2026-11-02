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


/* Logical replication-key metadata. This wire format is intentionally incompatible with older logs. */
#ifndef _REPLICATION_KEY_H_
#define _REPLICATION_KEY_H_

struct tp_domain;

typedef struct repl_key_column REPL_KEY_COLUMN;
struct repl_key_column
{
  int id;
  const char *name;
  struct tp_domain *domain;
  const char *domain_class_name;
  int descending;
  int not_null;
};

extern int repl_key_pack (const char *name, int type, int count, const REPL_KEY_COLUMN *columns,
                          char **data, int *length);
extern bool repl_key_unpack_log (const char *data, int length, const char **class_name, const char **key, int *key_length,
                                 const char **metadata, int *metadata_length);
extern bool repl_key_is_valid (const char *data, int length);
extern bool repl_key_equal (const char *left, int left_length, const char *right, int right_length);
extern void repl_key_describe (const char *data, int length, char *output, int capacity);

#if !defined (SERVER_MODE)
struct db_object;
extern int repl_key_from_class (struct db_object *class_obj, char **data, int *length);
#endif
#endif /* _REPLICATION_KEY_H_ */
