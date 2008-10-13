/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 *      ex.h: simplified object descriptions
 */

#ifndef _EX_H_
#define _EX_H_

#ident "$Id$"

#include "locator_cl.h"

extern char *database_name;
extern const char *output_dirname;
extern char *input_filename;
extern FILE *output_file;
extern struct text_output *obj_out;
extern int page_size;
extern int cached_pages;
extern int est_size;
extern char *hash_filename;
extern int debug_flag;
extern int verbose_flag;
extern int include_references;
extern char *output_prefix;
extern int do_schema;
extern int do_objects;
extern int delimited_id_flag;
extern bool ignore_err_flag;
extern int required_class_only;
extern LIST_MOPS *class_table;
extern DB_OBJECT **req_class_table;
extern int is_req_class (DB_OBJECT * class_);
extern int get_requested_classes (const char *input_filename,
				  DB_OBJECT * class_list[]);

extern int lo_count;

#define LEFT_DEL 	"\""
#define RIGHT_DEL	"\""
#define NO_DEL		""

#define PRINT_IDENTIFIER(s) need_quotes((s)) ? LEFT_DEL : NO_DEL, \
                            (s),                                  \
			    need_quotes((s)) ? RIGHT_DEL : NO_DEL

extern int extractschema (char *exec_name, int do_auth);
extern int extractobjects (char *exec_name);
extern bool need_quotes (const char *identifier);

#endif /* _EX_H_ */
