/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * do_class.h - This file contains do_class extern prototypes.
 */

#ifndef _DO_CLASS_H_
#define _DO_CLASS_H_

#ident "$Id$"

#include "dbi.h"

extern int do_add_queries (PARSER_CONTEXT * parser,
			   DB_CTMPL * ctemplate, const PT_NODE * queries);

extern int do_add_attributes (PARSER_CONTEXT * parser,
			      DB_CTMPL * ctemplate, PT_NODE * atts);

extern int do_add_constraints (DB_CTMPL * ctemplate, PT_NODE * constraints);

extern int do_add_methods (PARSER_CONTEXT * parser,
			   DB_CTMPL * ctemplate, PT_NODE * methods);

extern int do_add_method_files (const PARSER_CONTEXT * parser,
				DB_CTMPL * ctemplate, PT_NODE * method_files);

extern int do_add_resolutions (const PARSER_CONTEXT * parser,
			       DB_CTMPL * ctemplate,
			       const PT_NODE * resolution);

extern int do_add_supers (const PARSER_CONTEXT * parser,
			  DB_CTMPL * ctemplate, const PT_NODE * supers);

extern int do_add_foreign_key_objcache_attr (DB_CTMPL * ctemplate,
					     PT_NODE * constraints);

extern int do_set_object_id (const PARSER_CONTEXT * parser,
			     DB_CTMPL * ctemplate, PT_NODE * object_id_list);

extern int do_create_local (PARSER_CONTEXT * parser, DB_CTMPL * ctemplate,
			    PT_NODE * pt_node);

extern int do_create_entity (PARSER_CONTEXT * parser, PT_NODE * node);

#endif /* _DO_CLASS_H_ */
