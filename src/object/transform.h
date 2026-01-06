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
 * transform.h: Definitions for the transformer shared between the client and
 *            server.
 */

#ifndef _TRANSFORM_H_
#define _TRANSFORM_H_

#ident "$Id$"

#include "schema_system_catalog_constants.h"

/*
 * META_ATTRIBUTE, META_CLASS
 *    These are the structure definitions for the meta class information.
 *    They will be built statically and used for the generation of the
 *    catalog information for class objects.
 */

typedef struct tf_meta_attribute
{
  const char *ma_name;
  DB_TYPE ma_type;
  int ma_visible;		/* unused */
  const char *ma_domain_string;	/* unused */
  int ma_substructure;		/* unused */
  int ma_id;
  void *ma_extended_domain;	/* unused; filled in on the client side */
} META_ATTRIBUTE;

typedef struct tf_meta_class
{
  const char *mc_name;		/* unused */
  OID mc_classoid;
  int mc_repid;
  int mc_n_variable;
  int mc_fixed_size;
  META_ATTRIBUTE *mc_atts;
} META_CLASS;

#if !defined(CS_MODE)
typedef struct tf_ct_attribute
{
  const char *ca_name;
  int ca_id;
  DB_TYPE ca_type;
} CT_ATTR;

typedef struct tf_ct_class
{
  const char *cc_name;
  OID cc_classoid;
  int cc_n_atts;
  CT_ATTR *cc_atts;
} CT_CLASS;
#endif /* !CS_MODE */

/*
 * Meta OID information
 *    The meta-objects are given special system OIDs in the catalog.
 *    These don't map to actual physical locations but are used to
 *    tag the disk representations of classes with appropriate catalog
 *    keys.
 */

#define META_VOLUME			256

#define META_PAGE_CLASS			0
#define META_PAGE_ROOT			1
#define META_PAGE_REPRESENTATION	2
#define META_PAGE_RESOLUTION		3
#define META_PAGE_DOMAIN		4
#define META_PAGE_ATTRIBUTE		5
#define META_PAGE_METHARG		6
#define META_PAGE_METHSIG		7
#define META_PAGE_METHOD		8
#define META_PAGE_METHFILE		9
#define META_PAGE_REPATTRIBUTE		10
#define META_PAGE_QUERY_SPEC		11
#define META_PAGE_PARTITION		12

/*
 * Metaclass names
 *    Names for each of the meta classes.
 *    These can be used in query statements to query the schema.
 */

#define META_CLASS_NAME			"sqlx_class"
#define META_ATTRIBUTE_NAME		"sqlx_attribute"
#define META_DOMAIN_NAME		"sqlx_domain"
#define META_METHARG_NAME		"sqlx_method_argument"
#define META_METHSIG_NAME		"sqlx_method_signature"
#define META_METHOD_NAME		"sqlx_method"
#define META_METHFILE_NAME		"sqlx_method_file"
#define META_RESOLUTION_NAME		"sqlx_resolution"
#define META_REPRESENTATION_NAME	"sqlx_representation"
#define META_REPATTRIBUTE_NAME		"sqlx_repattribute"
#define META_QUERY_SPEC_NAME		"sqlx_query_spec"
#define META_PARTITION_NAME		"sqlx_partition"

#define SET_AUTO_INCREMENT_SERIAL_NAME(SR_NAME, CL_NAME, AT_NAME)  \
                         sprintf(SR_NAME, "%s_ai_%s", CL_NAME, AT_NAME)

#define AUTO_INCREMENT_SERIAL_NAME_EXTRA_LENGTH (4)

/*
 * AUTO_INCREMENT_SERIAL_NAME_MAX_LENGTH : (255 - 1) + 4 + (255 -1) + 1 = 513
 *   - sprintf (..., "%s_ai_%s", unique_name, attribute_name)
 */
#define AUTO_INCREMENT_SERIAL_NAME_MAX_LENGTH \
  ((DB_MAX_IDENTIFIER_LENGTH - 1) + AUTO_INCREMENT_SERIAL_NAME_EXTRA_LENGTH + (DB_MAX_IDENTIFIER_LENGTH - 1) + 1)
#define DB_MAX_SERIAL_NAME_LENGTH             (AUTO_INCREMENT_SERIAL_NAME_MAX_LENGTH)

/*
 * Metaclass definitions
 *    Static definitions of the meta classes.
 */

extern META_CLASS tf_Metaclass_root;
extern META_CLASS tf_Metaclass_class;
extern META_CLASS tf_Metaclass_representation;
extern META_CLASS tf_Metaclass_resolution;
extern META_CLASS tf_Metaclass_methfile;
extern META_CLASS tf_Metaclass_method;
extern META_CLASS tf_Metaclass_methsig;
extern META_CLASS tf_Metaclass_metharg;
extern META_CLASS tf_Metaclass_attribute;
extern META_CLASS tf_Metaclass_domain;
extern META_CLASS tf_Metaclass_repattribute;
extern META_CLASS tf_Metaclass_query_spec;
extern META_CLASS tf_Metaclass_partition;

#if !defined(CS_MODE)
extern CT_CLASS ct_Class;
extern CT_CLASS ct_Attribute;
extern CT_CLASS ct_Attrid;
extern CT_CLASS ct_Domain;
extern CT_CLASS ct_Method;
extern CT_CLASS ct_Methsig;
extern CT_CLASS ct_Metharg;
extern CT_CLASS ct_Methfile;
extern CT_CLASS ct_Resolution;
extern CT_CLASS ct_Queryspec;
extern CT_CLASS ct_Index;
extern CT_CLASS ct_Indexkey;
extern CT_CLASS ct_Partition;
extern CT_CLASS *ct_Classes[];

typedef enum
{
  CT_CLASS_CLASS_OF_INDEX = 0,
  CT_CLASS_INST_ATTR_COUNT_INDEX,
  CT_CLASS_SHARED_ATTR_COUNT_INDEX,
  CT_CLASS_INST_METH_COUNT_INDEX,
  CT_CLASS_CLASS_METH_COUNT_INDEX,
  CT_CLASS_CLASS_ATTR_COUNT_INDEX,
  CT_CLASS_IS_SYSTEM_CLASS_INDEX,
  CT_CLASS_CLASS_TYPE_INDEX,
  CT_CLASS_OWNER_INDEX,
  CT_CLASS_COLLATION_ID_INDEX,
  CT_CLASS_TDE_ALGORITHM_INDEX,
  CT_CLASS_STATISTICS_STRATEGY_INDEX,
  CT_CLASS_FLAGS_INDEX,
  CT_CLASS_CREATED_TIME_INDEX,
  CT_CLASS_UPDATED_TIME_INDEX,
  CT_CLASS_CHECKED_TIME_INDEX,
  CT_CLASS_UNIQUE_NAME_INDEX,
  CT_CLASS_CLASS_NAME_INDEX,
  CT_CLASS_SUB_CLASSES_INDEX,
  CT_CLASS_SUPER_CLASSES_INDEX,
  CT_CLASS_INST_ATTRS_INDEX,
  CT_CLASS_SHARED_ATTRS_INDEX,
  CT_CLASS_CLASS_ATTRS_INDEX,
  CT_CLASS_INST_METHS_INDEX,
  CT_CLASS_CLASS_METHS_INDEX,
  CT_CLASS_METH_FILES_INDEX,
  CT_CLASS_QUERY_SPECS_INDEX,
  CT_CLASS_INDEXES_INDEX,
  CT_CLASS_COMMENT_INDEX,
  CT_CLASS_PARTITION_INDEX,
} CT_ATTR_CLASS_INDEX;

typedef enum
{
  CT_INDEX_CLASS_OF_INDEX = 0,
  CT_INDEX_INDEX_NAME_INDEX,
  CT_INDEX_IS_UNIQUE_INDEX,
  CT_INDEX_KEY_COUNT_INDEX,
  CT_INDEX_KEY_ATTRS_INDEX,
  CT_INDEX_IS_REVERSE_INDEX,
  CT_INDEX_IS_PRIMARY_KEY_INDEX,
  CT_INDEX_IS_FOREIGN_KEY_INDEX,
  CT_INDEX_FILTER_EXPRESSION_INDEX,
  CT_INDEX_HAVE_FUNCTION_INDEX,
  CT_INDEX_STATUS_INDEX,
  CT_INDEX_REFERENTIAL_INDEX_INDEX,
  CT_INDEX_DELETE_RULE_INDEX,
  CT_INDEX_UPDATE_RULE_INDEX,
  CT_INDEX_REFERENTIAL_MATCH_OPTION_INDEX,
  CT_INDEX_INDEX_TYPE_INDEX,
  CT_INDEX_OPTIONS_INDEX,
  CT_INDEX_COMMENT_INDEX,
  CT_INDEX_CREATED_TIME_INDEX,
  CT_INDEX_UPDATED_TIME_INDEX,
} CT_ATTR_INDEX_INDEX;

// TODO: create CT_ATTR_*_INDEX of other CT_CLASSes
#endif /* !CS_MODE */

/* This fills in misc information missing from the static definitions */
extern void tf_compile_meta_classes (void);
extern bool tf_is_catalog_class (OID * class_oid);

/* This is available only on the server for catalog initialization */

#if !defined(CS_MODE)
extern int tf_install_meta_classes (void);
#endif /* !CS_MODE */

#endif /* _TRANSFORM_H_ */
