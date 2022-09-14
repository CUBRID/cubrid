/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
extern int est_size;
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
extern LIST_MOPS *class_table;
extern DB_OBJECT **req_class_table;
extern int is_req_class (DB_OBJECT * class_);
extern int get_requested_classes (const char *input_filename, DB_OBJECT * class_list[]);

extern int lo_count;

#define PRINT_IDENTIFIER(s) "[", (s), "]"
#define PRINT_FUNCTION_INDEX_NAME(s) "\"", (s), "\""

extern int extract_classes_to_file (extract_context & ctxt, const char *output_filename);
extern int extract_triggers (extract_context & ctxt, print_output & output_ctx);
extern int extract_triggers_to_file (extract_context & ctxt, const char *output_filename);
extern int extract_indexes_to_file (extract_context & ctxt, const char *output_filename);
extern int extract_classes (extract_context & ctxt, print_output & schema_output_ctx);
extern int extract_objects (const char *exec_name, const char *output_dirname, const char *output_prefix);

extern int create_filename_schema (const char *output_dirname, const char *output_prefix,
				   char *output_filename_p, const size_t filename_size);
extern int create_filename_trigger (const char *output_dirname, const char *output_prefix,
				    char *output_filename_p, const size_t filename_size);
extern int create_filename_indexes (const char *output_dirname, const char *output_prefix,
				    char *output_filename_p, const size_t filename_size);
#endif /* _UNLOADDB_H_ */
