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
 * transform.c: Definition of the meta-class information for class storage
 *              and catalog entries.
 */

#ident "$Id$"

#include "config.h"

#include <stdlib.h>
#include "error_manager.h"
#include "object_representation.h"
#include "object_domain.h"
#include "object_primitive.h"
#include "transform.h"

/* server side only */
#if !defined(CS_MODE)
#include "intl_support.h"
#include "language_support.h"
#include "system_catalog.h"
#endif /* !CS_MODE */

/*
 * These define the structure of the meta class objects
 *
 * IMPORTANT
 * If you modify either the META_ATTRIBUTE or META_CLASS definitions
 * here, make sure you adjust the associated ORC_ constants in or.h.
 * Of particular importance are ORC_CLASS_VAR_ATT_COUNT and
 * ORC_ATT_VAR_ATT_COUNT.
 * If you don't know what these are, you shouldn't be making this change.
 *
 */
/* DOMAIN */
static META_ATTRIBUTE domain_atts[] = {
  {"type", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"precision", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"scale", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"codeset", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"collation_id", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"class", DB_TYPE_OBJECT, 1, META_CLASS_NAME, 0, 0, NULL},
  {"enumeration", DB_TYPE_SET, 1, NULL, 0, 0, NULL},
  {"set_domain", DB_TYPE_SET, 1, META_DOMAIN_NAME, 1, 0, NULL},
  {NULL, (DB_TYPE) 0, 0, NULL, 0, 0, NULL}
};

META_CLASS tf_Metaclass_domain =
  { META_DOMAIN_NAME, {META_PAGE_DOMAIN, 0, META_VOLUME}, 0, 0, 0,
&domain_atts[0]
};

/* ATTRIBUTE */
static META_ATTRIBUTE att_atts[] = {
  {"id", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"type", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"offset", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"order", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"class", DB_TYPE_OBJECT, 1, META_CLASS_NAME, 0, 0, NULL},
  {"flags", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"index_fileid", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"index_root_pageid", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"index_volid_key", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"name", DB_TYPE_STRING, 1, NULL, 0, 0, NULL},
  {"value", DB_TYPE_VARIABLE, 1, NULL, 0, 0, NULL},
  {"original_value", DB_TYPE_VARIABLE, 0, NULL, 0, 0, NULL},
  {"domain", DB_TYPE_SET, 1, META_DOMAIN_NAME, 0, 0, NULL},
  {"triggers", DB_TYPE_SET, 1, "object", 0, 0, NULL},
  {"properties", DB_TYPE_SET, 0, NULL, 0, 0, NULL},
  {NULL, (DB_TYPE) 0, 0, NULL, 0, 0, NULL}
};
META_CLASS tf_Metaclass_attribute =
  { META_ATTRIBUTE_NAME, {META_PAGE_ATTRIBUTE, 0, META_VOLUME}, 0, 0, 0,
&att_atts[0]
};

/* METHOD ARGUMENT */
static META_ATTRIBUTE metharg_atts[] = {
  {"type", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"index", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"domain", DB_TYPE_SET, 1, META_DOMAIN_NAME, 1, 0, NULL},
  {NULL, (DB_TYPE) 0, 0, NULL, 0, 0, NULL}
};
META_CLASS tf_Metaclass_metharg =
  { META_METHARG_NAME, {META_PAGE_METHARG, 0, META_VOLUME}, 0, 0, 0,
&metharg_atts[0]
};

/* METHOD SIGNATURE */
static META_ATTRIBUTE methsig_atts[] = {
  {"arg_count", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"function_name", DB_TYPE_STRING, 1, NULL, 0, 0, NULL},
  {"string_def", DB_TYPE_STRING, 0, NULL, 0, 0, NULL},
  {"return_value", DB_TYPE_SET, 1, META_METHARG_NAME, 1, 0, NULL},
  {"arguments", DB_TYPE_SET, 1, META_METHARG_NAME, 1, 0, NULL},
  {NULL, (DB_TYPE) 0, 0, NULL, 0, 0, NULL}
};
META_CLASS tf_Metaclass_methsig =
  { META_METHSIG_NAME, {META_PAGE_METHSIG, 0, META_VOLUME}, 0, 0, 0,
&methsig_atts[0]
};

/* METHOD */
static META_ATTRIBUTE meth_atts[] = {
  {"class", DB_TYPE_OBJECT, 1, META_CLASS_NAME, 0, 0, NULL},
  {"id", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"name", DB_TYPE_STRING, 1, NULL, 0, 0, NULL},
  {"signatures", DB_TYPE_SET, 1, META_METHSIG_NAME, 1, 0, NULL},
  {"properties", DB_TYPE_SET, 0, NULL, 0, 0, NULL},
  {NULL, (DB_TYPE) 0, 0, NULL, 0, 0, NULL}
};
META_CLASS tf_Metaclass_method =
  { META_METHOD_NAME, {META_PAGE_METHOD, 0, META_VOLUME}, 0, 0, 0,
&meth_atts[0]
};

/* METHOD FILE */
static META_ATTRIBUTE methfile_atts[] = {
  {"class", DB_TYPE_OBJECT, 1, META_CLASS_NAME, 0, 0, NULL},
  {"name", DB_TYPE_STRING, 1, NULL, 0, 0, NULL},
  {"properties", DB_TYPE_SET, 0, NULL, 0, 0, NULL},
  {NULL, (DB_TYPE) 0, 0, NULL, 0, 0, NULL}
};
META_CLASS tf_Metaclass_methfile =
  { META_METHFILE_NAME, {META_PAGE_METHFILE, 0, META_VOLUME}, 0, 0, 0,
&methfile_atts[0]
};

/* RESOLUTION */
static META_ATTRIBUTE res_atts[] = {
  {"class", DB_TYPE_OBJECT, 1, META_CLASS_NAME, 0, 0, NULL},
  {"type", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"name", DB_TYPE_STRING, 1, NULL, 0, 0, NULL},
  {"alias", DB_TYPE_STRING, 1, NULL, 0, 0, NULL},
  {NULL, (DB_TYPE) 0, 0, NULL, 0, 0, NULL}
};
META_CLASS tf_Metaclass_resolution =
  { META_RESOLUTION_NAME, {META_PAGE_RESOLUTION, 0, META_VOLUME}, 0, 0, 0,
&res_atts[0]
};

/* REPATTRIBUTE */
static META_ATTRIBUTE repatt_atts[] = {
  {"id", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"type", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"domain", DB_TYPE_SET, 1, META_DOMAIN_NAME, 0, 0, NULL},
  {NULL, (DB_TYPE) 0, 0, NULL, 0, 0, NULL}
};
META_CLASS tf_Metaclass_repattribute =
  { META_REPATTRIBUTE_NAME, {META_PAGE_REPATTRIBUTE, 0, META_VOLUME}, 0, 0, 0,
&repatt_atts[0]
};

/* REPRESENTATION */
static META_ATTRIBUTE rep_atts[] = {
  {"id", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"fixed_count", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"variable_count", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"fixed_size", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"attributes", DB_TYPE_SET, 0, META_REPATTRIBUTE_NAME, 1, 0, NULL},
  {"properties", DB_TYPE_SET, 0, NULL, 0, 0, NULL},
  {NULL, (DB_TYPE) 0, 0, NULL, 0, 0, NULL}
};
META_CLASS tf_Metaclass_representation =
  { META_REPRESENTATION_NAME, {META_PAGE_REPRESENTATION, 0, META_VOLUME}, 0,
0, 0, &rep_atts[0]
};

/* CLASS */
static META_ATTRIBUTE class_atts[] = {
  {"attid_counter", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"methid_counter", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"unused", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"heap_fileid", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"heap_volid", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"heap_pageid", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"current_repid", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"fixed_count", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"variable_count", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"fixed_size", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"attribute_count", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"object_size", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"shared_count", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"method_count", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"class_method_count", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"class_att_count", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"flags", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"class_type", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"owner", DB_TYPE_OBJECT, 1, "object", 0, 0, NULL},
  {"collation_id", DB_TYPE_INTEGER, 1, NULL, 0, 0, NULL},
  {"name", DB_TYPE_STRING, 1, NULL, 0, 0, NULL},
  {"loader_commands", DB_TYPE_STRING, 1, NULL, 0, 0, NULL},
  {"representations", DB_TYPE_SET, 0, META_REPRESENTATION_NAME, 1, 0, NULL},
  {"sub_classes", DB_TYPE_SET, 1, META_CLASS_NAME, 0, 0, NULL},
  {"super_classes", DB_TYPE_SET, 1, META_CLASS_NAME, 0, 0, NULL},
  {"attributes", DB_TYPE_SET, 1, META_ATTRIBUTE_NAME, 1, 0, NULL},
  {"shared_attributes", DB_TYPE_SET, 1, META_ATTRIBUTE_NAME, 1, 0, NULL},
  {"class_attributes", DB_TYPE_SET, 1, META_ATTRIBUTE_NAME, 1, 0, NULL},
  {"methods", DB_TYPE_SET, 1, META_METHOD_NAME, 1, 0, NULL},
  {"class_methods", DB_TYPE_SET, 1, META_METHOD_NAME, 1, 0, NULL},
  {"method_files", DB_TYPE_SET, 1, META_METHFILE_NAME, 1, 0, NULL},
  {"resolutions", DB_TYPE_SET, 1, META_RESOLUTION_NAME, 1, 0, NULL},
  {"query_spec", DB_TYPE_SET, 1, META_QUERY_SPEC_NAME, 1, 0, NULL},
  {"triggers", DB_TYPE_SET, 1, "object", 0, 0, NULL},
  {"properties", DB_TYPE_SET, 0, NULL, 0, 0, NULL},
  {NULL, (DB_TYPE) 0, 0, NULL, 0, 0, NULL}
};
META_CLASS tf_Metaclass_class =
  { META_CLASS_NAME, {META_PAGE_CLASS, 0, META_VOLUME}, 0, 0, 0,
&class_atts[0]
};

/* QUERY_SPEC */
static META_ATTRIBUTE query_spec_atts[] = {
  {"specification", DB_TYPE_STRING, 1, NULL, 0, 0, NULL},
  {NULL, (DB_TYPE) 0, 0, NULL, 0, 0, NULL}
};
META_CLASS tf_Metaclass_query_spec =
  { META_QUERY_SPEC_NAME, {META_PAGE_QUERY_SPEC, 0, META_VOLUME}, 0, 0, 0,
&query_spec_atts[0]
};

/* ROOT */
static META_ATTRIBUTE root_atts[] = {
  {"heap_fileid", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"heap_volid", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"heap_pageid", DB_TYPE_INTEGER, 0, NULL, 0, 0, NULL},
  {"name", DB_TYPE_STRING, 0, NULL, 0, 0, NULL},
  {"sub_classes", DB_TYPE_SET, 0, NULL, 0, 0, NULL},
  {NULL, (DB_TYPE) 0, 0, NULL, 0, 0, NULL}
};
META_CLASS tf_Metaclass_root =
  { "rootclass", {META_PAGE_ROOT, 0, META_VOLUME}, 0, 0, 0, &root_atts[0] };

/*
 * Meta_classes
 *    An array of pointers to each meta class.  This is used to reference
 *    the class structures after they have been compiled.
 */
static META_CLASS *Meta_classes[] = {
  &tf_Metaclass_root,
  &tf_Metaclass_class,
  &tf_Metaclass_representation,
  &tf_Metaclass_resolution,
  &tf_Metaclass_methfile,
  &tf_Metaclass_method,
  &tf_Metaclass_methsig,
  &tf_Metaclass_metharg,
  &tf_Metaclass_attribute,
  &tf_Metaclass_domain,
  &tf_Metaclass_repattribute,
  &tf_Metaclass_query_spec,
  NULL
};

#if !defined(CS_MODE)

static CT_ATTR ct_class_atts[] = {
  {"class_of", NULL_ATTRID, DB_TYPE_OBJECT},
  {"inst_attr_count", NULL_ATTRID, DB_TYPE_INTEGER},
  {"shared_attr_count", NULL_ATTRID, DB_TYPE_INTEGER},
  {"inst_meth_count", NULL_ATTRID, DB_TYPE_INTEGER},
  {"class_meth_count", NULL_ATTRID, DB_TYPE_INTEGER},
  {"class_attr_count", NULL_ATTRID, DB_TYPE_INTEGER},
  {"is_system_class", NULL_ATTRID, DB_TYPE_INTEGER},
  {"class_type", NULL_ATTRID, DB_TYPE_INTEGER},
  {"owner", NULL_ATTRID, DB_TYPE_OBJECT},
  {"collation_id", NULL_ATTRID, DB_TYPE_INTEGER},
  {"class_name", NULL_ATTRID, DB_TYPE_VARCHAR},
  {"sub_classes", NULL_ATTRID, DB_TYPE_SEQUENCE},
  {"super_classes", NULL_ATTRID, DB_TYPE_SEQUENCE},
  {"inst_attrs", NULL_ATTRID, DB_TYPE_SEQUENCE},
  {"shared_attrs", NULL_ATTRID, DB_TYPE_SEQUENCE},
  {"class_attrs", NULL_ATTRID, DB_TYPE_SEQUENCE},
  {"inst_meths", NULL_ATTRID, DB_TYPE_SEQUENCE},
  {"class_meths", NULL_ATTRID, DB_TYPE_SEQUENCE},
  {"meth_files", NULL_ATTRID, DB_TYPE_SEQUENCE},
  {"query_specs", NULL_ATTRID, DB_TYPE_SEQUENCE},
  {"indexes", NULL_ATTRID, DB_TYPE_SEQUENCE}
};

static CT_ATTR ct_attribute_atts[] = {
  {"class_of", NULL_ATTRID, DB_TYPE_OBJECT},
  {"attr_name", NULL_ATTRID, DB_TYPE_VARCHAR},
  {"attr_type", NULL_ATTRID, DB_TYPE_INTEGER},
  {"from_attr_name", NULL_ATTRID, DB_TYPE_VARCHAR},
  {"data_type", NULL_ATTRID, DB_TYPE_INTEGER},
  {"def_order", NULL_ATTRID, DB_TYPE_INTEGER},
  {"from_class_of", NULL_ATTRID, DB_TYPE_OBJECT},
  {"is_nullable", NULL_ATTRID, DB_TYPE_INTEGER},
  {"default_value", NULL_ATTRID, DB_TYPE_VARCHAR},
  {"domains", NULL_ATTRID, DB_TYPE_SEQUENCE}
};

static CT_ATTR ct_attrid_atts[] = {
  {"id", NULL_ATTRID, DB_TYPE_INTEGER},
  {"name", NULL_ATTRID, DB_TYPE_VARCHAR}
};

static CT_ATTR ct_domain_atts[] = {
  {"object_of", NULL_ATTRID, DB_TYPE_OBJECT},
  {"data_type", NULL_ATTRID, DB_TYPE_INTEGER},
  {"prec", NULL_ATTRID, DB_TYPE_INTEGER},
  {"scale", NULL_ATTRID, DB_TYPE_INTEGER},
  {"code_set", NULL_ATTRID, DB_TYPE_INTEGER},
  {"collation_id", NULL_ATTRID, DB_TYPE_INTEGER},
  {"class_of", NULL_ATTRID, DB_TYPE_OBJECT},
  {"enumeration", NULL_ATTRID, DB_TYPE_SEQUENCE},
  {"set_domains", NULL_ATTRID, DB_TYPE_SEQUENCE}
};

static CT_ATTR ct_method_atts[] = {
  {"class_of", NULL_ATTRID, DB_TYPE_OBJECT},
  {"meth_name", NULL_ATTRID, DB_TYPE_VARCHAR},
  {"meth_type", NULL_ATTRID, DB_TYPE_INTEGER},
  {"from_meth_name", NULL_ATTRID, DB_TYPE_VARCHAR},
  {"from_class_of", NULL_ATTRID, DB_TYPE_OBJECT},
  {"signatures", NULL_ATTRID, DB_TYPE_SEQUENCE}
};

static CT_ATTR ct_methsig_atts[] = {
  {"meth_of", NULL_ATTRID, DB_TYPE_OBJECT},
  {"arg_count", NULL_ATTRID, DB_TYPE_INTEGER},
  {"func_name", NULL_ATTRID, DB_TYPE_VARCHAR},
  {"return_value", NULL_ATTRID, DB_TYPE_SEQUENCE},
  {"arguments", NULL_ATTRID, DB_TYPE_SEQUENCE}
};

static CT_ATTR ct_metharg_atts[] = {
  {"meth_sig_of", NULL_ATTRID, DB_TYPE_OBJECT},
  {"data_type", NULL_ATTRID, DB_TYPE_INTEGER},
  {"index_of", NULL_ATTRID, DB_TYPE_INTEGER},
  {"domains", NULL_ATTRID, DB_TYPE_SEQUENCE}
};

static CT_ATTR ct_methfile_atts[] = {
  {"class_of", NULL_ATTRID, DB_TYPE_OBJECT},
  {"from_class_of", NULL_ATTRID, DB_TYPE_OBJECT},
  {"path_name", NULL_ATTRID, DB_TYPE_VARCHAR}
};

static CT_ATTR ct_queryspec_atts[] = {
  {"class_of", NULL_ATTRID, DB_TYPE_OBJECT},
  {"spec", NULL_ATTRID, DB_TYPE_VARCHAR}
};

static CT_ATTR ct_resolution_atts[] = {
  {"class_of", NULL_ATTRID, DB_TYPE_OBJECT},
  {"alias", NULL_ATTRID, DB_TYPE_VARCHAR},
  {"namespace", NULL_ATTRID, DB_TYPE_INTEGER},
  {"res_name", NULL_ATTRID, DB_TYPE_VARCHAR}
};

static CT_ATTR ct_index_atts[] = {
  {"class_of", NULL_ATTRID, DB_TYPE_OBJECT},
  {"index_name", NULL_ATTRID, DB_TYPE_VARCHAR},
  {"is_unique", NULL_ATTRID, DB_TYPE_INTEGER},
  {"key_count", NULL_ATTRID, DB_TYPE_INTEGER},
  {"key_attrs", NULL_ATTRID, DB_TYPE_SEQUENCE},
  {"is_reverse", NULL_ATTRID, DB_TYPE_INTEGER},
  {"is_primary_key", NULL_ATTRID, DB_TYPE_INTEGER},
  {"is_foreign_key", NULL_ATTRID, DB_TYPE_INTEGER},
  {"filter_expression", NULL_ATTRID, DB_TYPE_VARCHAR},
  {"have_function", NULL_ATTRID, DB_TYPE_INTEGER}
};

static CT_ATTR ct_indexkey_atts[] = {
  {"index_of", NULL_ATTRID, DB_TYPE_OBJECT},
  {"key_attr_name", NULL_ATTRID, DB_TYPE_VARCHAR},
  {"key_order", NULL_ATTRID, DB_TYPE_INTEGER},
  {"asc_desc", NULL_ATTRID, DB_TYPE_INTEGER},
  {"key_prefix_length", NULL_ATTRID, DB_TYPE_INTEGER},
  {"func", NULL_ATTRID, DB_TYPE_VARCHAR}
};

#define NULL_OID_INITIALIZER    {NULL_PAGEID, NULL_SLOTID, NULL_VOLID}

CT_CLASS ct_Class = {
  CT_CLASS_NAME,
  NULL_OID_INITIALIZER,
  (sizeof (ct_class_atts) / sizeof (ct_class_atts[0])),
  ct_class_atts
};

CT_CLASS ct_Attribute = {
  CT_ATTRIBUTE_NAME,
  NULL_OID_INITIALIZER,
  (sizeof (ct_attribute_atts) / sizeof (ct_attribute_atts[0])),
  ct_attribute_atts
};

CT_CLASS ct_Attrid = {
  NULL,
  NULL_OID_INITIALIZER,
  (sizeof (ct_attrid_atts) / sizeof (ct_attrid_atts[0])),
  ct_attrid_atts
};

CT_CLASS ct_Domain = {
  CT_DOMAIN_NAME,
  NULL_OID_INITIALIZER,
  (sizeof (ct_domain_atts) / sizeof (ct_domain_atts[0])),
  ct_domain_atts
};

CT_CLASS ct_Method = {
  CT_METHOD_NAME,
  NULL_OID_INITIALIZER,
  (sizeof (ct_method_atts) / sizeof (ct_method_atts[0])),
  ct_method_atts
};

CT_CLASS ct_Methsig = {
  CT_METHSIG_NAME,
  NULL_OID_INITIALIZER,
  (sizeof (ct_methsig_atts) / sizeof (ct_methsig_atts[0])),
  ct_methsig_atts
};

CT_CLASS ct_Metharg = {
  CT_METHARG_NAME,
  NULL_OID_INITIALIZER,
  (sizeof (ct_metharg_atts) / sizeof (ct_metharg_atts[0])),
  ct_metharg_atts
};

CT_CLASS ct_Methfile = {
  CT_METHFILE_NAME,
  NULL_OID_INITIALIZER,
  (sizeof (ct_methfile_atts) / sizeof (ct_methfile_atts[0])),
  ct_methfile_atts
};

CT_CLASS ct_Queryspec = {
  CT_QUERYSPEC_NAME,
  NULL_OID_INITIALIZER,
  (sizeof (ct_queryspec_atts) / sizeof (ct_queryspec_atts[0])),
  ct_queryspec_atts
};

CT_CLASS ct_Resolution = {
  NULL,
  NULL_OID_INITIALIZER,
  (sizeof (ct_resolution_atts) / sizeof (ct_resolution_atts[0])),
  ct_resolution_atts
};

CT_CLASS ct_Index = {
  CT_INDEX_NAME,
  NULL_OID_INITIALIZER,
  (sizeof (ct_index_atts) / sizeof (ct_index_atts[0])),
  ct_index_atts
};

CT_CLASS ct_Indexkey = {
  CT_INDEXKEY_NAME,
  NULL_OID_INITIALIZER,
  (sizeof (ct_indexkey_atts) / sizeof (ct_indexkey_atts[0])),
  ct_indexkey_atts
};

CT_CLASS *ct_Classes[] = {
  &ct_Class,
  &ct_Attribute,
  &ct_Domain,
  &ct_Method,
  &ct_Methsig,
  &ct_Metharg,
  &ct_Methfile,
  &ct_Queryspec,
  &ct_Index,
  &ct_Indexkey,
  NULL
};

bool
tf_is_catalog_class (OID * class_oid)
{
  int c;

  for (c = 0; ct_Classes[c] != NULL; c++)
    {
      if (OID_EQ (&ct_Classes[c]->classoid, class_oid))
	{
	  return true;
	}
    }

  return false;
}

#endif /* !CS_MODE */
/*
 * tf_compile_meta_classes - passes over the static meta class definitions
 * and fills in the missing fields that are too error prone to keep
 * calculating by hand.
 *    return: void
 * Note:
 *   Once this becomes reasonably static, this could be statically coded again.
 *   This is only used on the client but it won't hurt anything to have it on
 *   the server as well.
 */
void
tf_compile_meta_classes ()
{
  META_CLASS *class_;
  META_ATTRIBUTE *att;
  TP_DOMAIN *domain;
  int c, i;

  for (c = 0; Meta_classes[c] != NULL; c++)
    {
      class_ = Meta_classes[c];

      class_->n_variable = class_->fixed_size = 0;

      for (i = 0; class_->atts[i].name != NULL; i++)
	{
	  att = &class_->atts[i];
	  att->id = i;

	  if (pr_is_variable_type (att->type))
	    {
	      class_->n_variable++;
	    }
	  else if (class_->n_variable)
	    {
	      /*
	       * can't have fixed width attributes AFTER variable width
	       * attributes
	       */
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE,
		      ER_TF_INVALID_METACLASS, 0);
	    }
	  else
	    {
	      /*
	       * need a domain for size calculations, since we don't use
	       * any parameterized types this isn't necessary but we still must
	       * have it to call tp_domain_isk_size().
	       */
	      domain = tp_domain_resolve_default (att->type);
	      class_->fixed_size += tp_domain_disk_size (domain);
	    }
	}
    }
}

#if !defined(CS_MODE)
/*
 * tf_install_meta_classes - dummy function
 *    return: NO_ERROR
 * Note:
 *    This is called during database formatting and generates the catalog
 *    entries for all the meta classes.
 */
int
tf_install_meta_classes ()
{
  /*
   * no longer making catalog entries, eventually build the meta-class object
   * here
   */
  return NO_ERROR;
}
#endif /* CS_MODE */
