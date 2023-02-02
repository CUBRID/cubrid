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
 *      unloaddb.h: simplified object descriptions
 */

#ifndef _UNLOADDB_H_
#define _UNLOADDB_H_

#ident "$Id$"

#include "locator_cl.h"



struct extract_context;
class print_output;

extern char *database_name;
extern char *input_filename;
extern struct text_output *obj_out;
extern int page_size;
extern int cached_pages;
extern int64_t est_size;
extern char *hash_filename;
extern int debug_flag;
extern bool verbose_flag;
extern bool latest_image_flag;
extern bool include_references;
extern bool do_schema;
extern bool do_objects;
extern bool ignore_err_flag;
extern bool required_class_only;
extern bool datafile_per_class;
extern bool each_schema_info;
extern LIST_MOPS *class_table;
extern DB_OBJECT **req_class_table;
extern int is_req_class (DB_OBJECT * class_);
extern int get_requested_classes (const char *input_filename, DB_OBJECT * class_list[]);

extern int lo_count;

#define PRINT_IDENTIFIER(s) "[", (s), "]"
#define PRINT_IDENTIFIER_WITH_QUOTE(s) "\"", (s), "\""
#define PRINT_FUNCTION_INDEX_NAME(s) "\"", (s), "\""

/* 
 * name is user_specified_name.
 * owner_name must be a char array of size DB_MAX_IDENTIFIER_LENGTH to copy user_specified_name.
 * class_name refers to class_name after dot(.).
 */
#define SPLIT_USER_SPECIFIED_NAME(name, owner_name, class_name) \
	do \
	  { \
	    assert (strlen ((name)) < STATIC_CAST (int, sizeof ((owner_name)))); \
	    strcpy ((owner_name), (name)); \
	    (class_name) = strchr ((owner_name), '.'); \
	    *(class_name)++ = '\0'; \
	  } \
	while (0)

extern int extract_classes_to_file (extract_context & ctxt);
extern int extract_triggers (extract_context & ctxt, print_output & output_ctx);
extern int extract_triggers_to_file (extract_context & ctxt, const char *output_filename);
extern int extract_indexes_to_file (extract_context & ctxt, const char *output_filename);
extern int extract_objects (const char *exec_name, const char *output_dirname, const char *output_prefix);

extern int create_filename_schema (const char *output_dirname, const char *output_prefix,
				   char *output_filename_p, const size_t filename_size);
extern int create_filename_trigger (const char *output_dirname, const char *output_prefix,
				    char *output_filename_p, const size_t filename_size);
extern int create_filename_indexes (const char *output_dirname, const char *output_prefix,
				    char *output_filename_p, const size_t filename_size);
#endif /* _UNLOADDB_H_ */
