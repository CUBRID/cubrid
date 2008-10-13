/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * schema_manager_1.h - "Schema" (in the SQL standard sense) implementation
 * TODO: merge this file into  schema_manager_2.h
 */

#ifndef _SCHEMA_MANAGER_1_H_
#define _SCHEMA_MANAGER_1_H_

#ident "$Id$"

/* currently this is a private function to be called only by AU_SET_USER */
extern int sc_set_current_schema (MOP user);
/* Obtain (pointer to) current schema name.                            */
extern const char *sc_current_schema_name (void);

#endif /* _SCHEMA_MANAGER_1_H_ */
