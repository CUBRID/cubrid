/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by 
 *   the Free Software Foundation; version 2 of the License. 
 *
 *  This program is distributed in the hope that it will be useful, 
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 *  GNU General Public License for more details. 
 *
 *  You should have received a copy of the GNU General Public License 
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA 
 *
 */


/*
 *      loader_action.h: Definitions for loader action routines
 */

#ifndef _LOADER_ACTION_H_
#define _LOADER_ACTION_H_

#ident "$Id$"

typedef enum
{
  SYS_ELO_INTERNAL,
  SYS_ELO_EXTERNAL,
  SYS_USER,
  SYS_CLASS
} ACT_SYSOBJ_TYPE;

extern void act_init (void);
extern void act_finish (int error);
extern void act_newline (void);

extern void display_error_line (int adjust);

extern void act_start_id (char *classname);
extern void act_set_id (int id);

extern void act_set_class (char *classname);
extern void act_class_attributes (void);
extern void act_shared_attributes (void);
extern void act_default_attributes (void);
extern void act_add_attribute (char *attname);
extern void act_add_instance (int id);
extern void act_set_ref_class (char *name);
extern void act_set_ref_class_id (int id);

extern void act_start_set (void);
extern void act_end_set (void);

extern void act_reference (int id);
extern void act_reference_class (void);
extern void act_int (char *token);
extern void act_real (char *token);
extern void act_monetary (char *token);
extern void act_date (char *token);
extern void act_time (char *token, int type);
extern void act_string (char *token, int size, DB_TYPE dtype);
extern void act_null (void);

extern void act_set_constructor (char *name);
extern void act_add_argument (char *name);

extern void act_nstring (char *);
extern void act_bstring (char *, int);


extern void act_system (const char *text, ACT_SYSOBJ_TYPE type);

#endif /* _LOADER_ACTION_H_ */
