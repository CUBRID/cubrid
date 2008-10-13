/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * execute_serial.h - This file contains do_serial extern prototypes.
 * TODO: rename this file to execute_serial.h
 */

#ifndef _EXECUTE_SERIAL_H_
#define _EXECUTE_SERIAL_H_

#ident "$Id$"

#include "dbi.h"
#include "parser.h"

extern int do_update_auto_increment_serial_on_rename (MOP serial_obj,
						      const char *class_name,
						      const char *att_name);


extern int do_get_serial_obj_id (DB_IDENTIFIER * serial_obj_id, int *found,
				 const char *serial_name);

extern int do_create_serial (PARSER_CONTEXT * parser, PT_NODE * statement);

extern int do_create_auto_increment_serial (PARSER_CONTEXT * parser,
					    MOP * serial_object,
					    const char *class_name,
					    PT_NODE * att);

extern int do_alter_serial (PARSER_CONTEXT * parser, PT_NODE * statement);
extern int do_drop_serial (PARSER_CONTEXT * parser, PT_NODE * statement);

#endif /* _EXECUTE_SERIAL_H_ */
