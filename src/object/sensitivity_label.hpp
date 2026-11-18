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
 * sensitivity_label.hpp - Sensitivity labels and their components in the catalog
 */

#ifndef _SENSITIVITY_LABEL_HPP_
#define _SENSITIVITY_LABEL_HPP_

#include "class_object.h"
#include "dbtype_def.h"
#include "misctype_def.h"
#include "object_fetch.h"

int sensitivity_label_create_component (const PT_MISC_TYPE kind, const char *name, const int level,
					const char *comment);
int sensitivity_label_alter_component (const PT_MISC_TYPE kind, const char *name, const char *new_name,
				       const int level, const char *comment);
int sensitivity_label_drop_component (const PT_MISC_TYPE kind, const char *name);
int sensitivity_label_find_component (const PT_MISC_TYPE kind, const char *name, const AU_FETCHMODE fetchmode,
				      DB_OBJECT **component);

int sensitivity_label_find_class (const char *name, MOP *class_mop, SM_CLASS **class_);
int sensitivity_label_find_attribute (const char *class_name, const char *att_name, MOP *class_mop, SM_CLASS **class_,
				      SM_ATTRIBUTE **att);
bool sensitivity_label_is_allowed (SM_CLASS *class_);
int sensitivity_label_find (MOP class_mop, const int attr_id, DB_OBJECT **label);
int sensitivity_label_create (MOP class_mop, const int attr_id, DB_OBJECT *grade, DB_OBJECT *info_type,
			      DB_OBJECT *source);
int sensitivity_label_replace (DB_OBJECT *label, DB_OBJECT *grade, DB_OBJECT *info_type, DB_OBJECT *source);
int sensitivity_label_drop (DB_OBJECT *label);
int sensitivity_label_drop_all (MOP class_mop, SM_CLASS *class_, DB_NAMELIST **dropped);
int sensitivity_label_create_on_attribute (const char *class_name, const char *att_name, DB_OBJECT *grade,
    DB_OBJECT *info_type, DB_OBJECT *source, const bool or_replace);
int sensitivity_label_drop_on_attribute (const char *class_name, const char *att_name);

#endif /* _SENSITIVITY_LABEL_HPP_ */
