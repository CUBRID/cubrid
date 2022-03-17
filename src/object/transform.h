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

/* catalog classes */
#define CT_CLASS_NAME              "_db_class"
#define CT_ATTRIBUTE_NAME          "_db_attribute"
#define CT_DOMAIN_NAME             "_db_domain"
#define CT_METHOD_NAME             "_db_method"
#define CT_METHSIG_NAME            "_db_meth_sig"
#define CT_METHARG_NAME            "_db_meth_arg"
#define CT_METHFILE_NAME           "_db_meth_file"
#define CT_QUERYSPEC_NAME          "_db_query_spec"
#define CT_RESOLUTION_NAME         "_db_resolution"
#define CT_INDEX_NAME              "_db_index"
#define CT_INDEXKEY_NAME           "_db_index_key"
#define CT_CLASSAUTH_NAME          "_db_auth"
#define CT_DATATYPE_NAME           "_db_data_type"
#define CT_STORED_PROC_NAME        "_db_stored_procedure"
#define CT_STORED_PROC_ARGS_NAME   "_db_stored_procedure_args"
#define CT_PARTITION_NAME          "_db_partition"
#define CT_SERIAL_NAME             "db_serial"
#define CT_HA_APPLY_INFO_NAME      "db_ha_apply_info"
#define CT_COLLATION_NAME          "_db_collation"
#define CT_USER_NAME               "db_user"
#define CT_TRIGGER_NAME            "db_trigger"
#define CT_ROOT_NAME               "db_root"
#define CT_PASSWORD_NAME           "db_password"
#define CT_AUTHORIZATION_NAME      "db_authorization"
#define CT_AUTHORIZATIONS_NAME     "db_authorizations"
#define CT_CHARSET_NAME		   "_db_charset"
#define CT_DUAL_NAME               "dual"
#define CT_DB_SERVER_NAME          "_db_server"

/* catalog vclasses */
#define CTV_CLASS_NAME             "db_class"
#define CTV_SUPER_CLASS_NAME       "db_direct_super_class"
#define CTV_VCLASS_NAME            "db_vclass"
#define CTV_ATTRIBUTE_NAME         "db_attribute"
#define CTV_ATTR_SD_NAME           "db_attr_setdomain_elm"
#define CTV_METHOD_NAME            "db_method"
#define CTV_METHARG_NAME           "db_meth_arg"
#define CTV_METHARG_SD_NAME        "db_meth_arg_setdomain_elm"
#define CTV_METHFILE_NAME          "db_meth_file"
#define CTV_INDEX_NAME             "db_index"
#define CTV_INDEXKEY_NAME          "db_index_key"
#define CTV_AUTH_NAME              "db_auth"
#define CTV_TRIGGER_NAME           "db_trig"
#define CTV_STORED_PROC_NAME       "db_stored_procedure"
#define CTV_STORED_PROC_ARGS_NAME  "db_stored_procedure_args"
#define CTV_PARTITION_NAME         "db_partition"
#define CTV_DB_COLLATION_NAME      "db_collation"
#define CTV_DB_CHARSET_NAME	   "db_charset"
#define CTV_DB_SERVER_NAME         "db_server"

#define CT_DBCOLL_COLL_ID_COLUMN	   "coll_id"
#define CT_DBCOLL_COLL_NAME_COLUMN	   "coll_name"
#define CT_DBCOLL_CHARSET_ID_COLUMN	   "charset_id"
#define CT_DBCOLL_BUILT_IN_COLUMN	   "built_in"
#define CT_DBCOLL_EXPANSIONS_COLUMN	   "expansions"
#define CT_DBCOLL_CONTRACTIONS_COLUMN	   "contractions"
#define CT_DBCOLL_UCA_STRENGTH		   "uca_strength"
#define CT_DBCOLL_CHECKSUM_COLUMN	   "checksum"

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
#endif /* !CS_MODE */

/* This fills in misc information missing from the static definitions */
extern void tf_compile_meta_classes (void);
extern bool tf_is_catalog_class (OID * class_oid);

/* This is available only on the server for catalog initialization */

#if !defined(CS_MODE)
extern int tf_install_meta_classes (void);
#endif /* !CS_MODE */

#endif /* _TRANSFORM_H_ */
