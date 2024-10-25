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
 * oid.c - object identifier (OID) module  (at client and server)
 */

#ident "$Id$"

#include "config.h"

#include <assert.h>

#include "oid.h"
#include "schema_system_catalog_constants.h"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

typedef struct oid_cache_entry OID_CACHE_ENTRY;
struct oid_cache_entry
{
  OID *oid;
  const char *class_name;
};

static OID oid_Root_class = { 0, 0, 0 };
static OID oid_Serial_class = { 0, 0, 0 };
static OID oid_Partition_class = { 0, 0, 0 };
static OID oid_Collation_class = { 0, 0, 0 };
static OID oid_HA_apply_info_class = { 0, 0, 0 };
static OID oid_Class_class = { 0, 0, 0 };
static OID oid_Attribute_class = { 0, 0, 0 };
static OID oid_Domain_class = { 0, 0, 0 };
static OID oid_Method_class = { 0, 0, 0 };
static OID oid_Methsig_class = { 0, 0, 0 };
static OID oid_Metharg_class = { 0, 0, 0 };
static OID oid_Methfile_class = { 0, 0, 0 };
static OID oid_Queryspec_class = { 0, 0, 0 };
static OID oid_Index_class = { 0, 0, 0 };
static OID oid_Indexkey_class = { 0, 0, 0 };
static OID oid_Datatype_class = { 0, 0, 0 };
static OID oid_Classauth_class = { 0, 0, 0 };
static OID oid_Stored_proc_class = { 0, 0, 0 };
static OID oid_Stored_proc_args_class = { 0, 0, 0 };
static OID oid_Charset_class = { 0, 0, 0 };
static OID oid_Trigger_class = { 0, 0, 0 };
static OID oid_User_class = { 0, 0, 0 };
static OID oid_Password_class = { 0, 0, 0 };
static OID oid_Authorization_class = { 0, 0, 0 };
static OID oid_Authorizations_class = { 0, 0, 0 };
static OID oid_DB_root_class = { 0, 0, 0 };
static OID oid_DBServer_class = { 0, 0, 0 };
static OID oid_Synonym_class = { 0, 0, 0 };

static OID oid_Rep_Read_Tran = { 0, (short int) 0x8000, 0 };

const OID oid_Null_oid = { NULL_PAGEID, NULL_SLOTID, NULL_VOLID };

PAGEID oid_Next_tempid = NULL_PAGEID;

/* ROOT_CLASS OID values. Set during restart/initialization.*/
OID *oid_Root_class_oid = &oid_Root_class;


OID *oid_Serial_class_oid = &oid_Serial_class;
OID *oid_Partition_class_oid = &oid_Partition_class;
OID *oid_User_class_oid = &oid_User_class;


OID_CACHE_ENTRY oid_Cache[OID_CACHE_SIZE] = {
  {&oid_Root_class, NULL},	/* Root class is not identifiable by a name */
  {&oid_Class_class, CT_CLASS_NAME},
  {&oid_Attribute_class, CT_ATTRIBUTE_NAME},
  {&oid_Domain_class, CT_DOMAIN_NAME},
  {&oid_Method_class, CT_METHOD_NAME},
  {&oid_Methsig_class, CT_METHSIG_NAME},
  {&oid_Metharg_class, CT_METHARG_NAME},
  {&oid_Methfile_class, CT_METHFILE_NAME},
  {&oid_Queryspec_class, CT_QUERYSPEC_NAME},
  {&oid_Index_class, CT_INDEX_NAME},
  {&oid_Indexkey_class, CT_INDEXKEY_NAME},
  {&oid_Datatype_class, CT_DATATYPE_NAME},
  {&oid_Classauth_class, CT_CLASSAUTH_NAME},
  {&oid_Partition_class, CT_PARTITION_NAME},
  {&oid_Stored_proc_class, CT_STORED_PROC_NAME},
  {&oid_Stored_proc_args_class, CT_STORED_PROC_ARGS_NAME},
  {&oid_Serial_class, CT_SERIAL_NAME},
  {&oid_HA_apply_info_class, CT_HA_APPLY_INFO_NAME},
  {&oid_Collation_class, CT_COLLATION_NAME},
  {&oid_Charset_class, CT_CHARSET_NAME},
  {&oid_Trigger_class, CT_TRIGGER_NAME},
  {&oid_User_class, CT_USER_NAME},
  {&oid_Password_class, CT_PASSWORD_NAME},
  {&oid_Authorization_class, CT_AUTHORIZATION_NAME},
  {&oid_Authorizations_class, CT_AUTHORIZATIONS_NAME},
  {&oid_DB_root_class, CT_ROOT_NAME},
  {&oid_DBServer_class, CT_DB_SERVER_NAME},
  {&oid_Synonym_class, CT_SYNONYM_NAME}
};

/*
 * oid_set_root() -  Set the value of the root oid to the given value
 *   return: void
 *   oid(in): Rootoid value
 *
 * Note:  This function is used during restart/initialization.
 */
void
oid_set_root (const OID * oid)
{
  oid_Root_class_oid = &oid_Root_class;
  if (oid != oid_Root_class_oid)
    {
      oid_Root_class_oid->volid = oid->volid;
      oid_Root_class_oid->pageid = oid->pageid;
      oid_Root_class_oid->slotid = oid->slotid;
    }
}

/*
 * oid_is_root() - Find out if the passed oid is the root oid
 *   return: true/false
 *   oid(in): Object identifier
 */
bool
oid_is_root (const OID * oid)
{
  return OID_EQ (oid, oid_Root_class_oid);
}

/*
 * oid_set_serial () - Store serial class OID
 *
 * return   :
 * oid (in) :
 */
void
oid_set_serial (const OID * oid)
{
  COPY_OID (oid_Serial_class_oid, oid);
}

/*
 * ois_is_serial () - Compare OID with serial class OID.
 *
 * return :
 * const OID * oid (in) :
 */
bool
oid_is_serial (const OID * oid)
{
  return OID_EQ (oid, oid_Serial_class_oid);
}

/*
 * oid_get_serial_oid () - Get serial class OID
 *
 * return    : Void.
 * oid (out) : Serial class OID.
 */
void
oid_get_serial_oid (OID * oid)
{
  COPY_OID (oid, oid_Serial_class_oid);
}

/*
 * oid_set_partition () - Store _db_partition class OID
 *
 * return   :
 * oid (in) :
 */
void
oid_set_partition (const OID * oid)
{
  COPY_OID (oid_Partition_class_oid, oid);
}

/*
 * ois_is_partition () - Compare OID with partition class OID.
 *
 * return :
 * const OID * oid (in) :
 */
bool
oid_is_partition (const OID * oid)
{
  return OID_EQ (oid, oid_Partition_class_oid);
}

/*
 * oid_get_partition_oid () - Get partition class OID
 *
 * return    : Void.
 * oid (out) : Serial class OID.
 */
void
oid_get_partition_oid (OID * oid)
{
  COPY_OID (oid, oid_Partition_class_oid);
}

/*
 * oid_is_db_class () - Is this OID of db_class?
 *
 * return   : True/false.
 * oid (in) : Check OID.
 */
bool
oid_is_db_class (const OID * oid)
{
  return OID_EQ (oid, &oid_Class_class);
}

/*
 * oid_is_db_attribute () - Is this OID of db_attribute?
 *
 * return   : True/false.
 * oid (in) : Check OID.
 */
bool
oid_is_db_attribute (const OID * oid)
{
  return OID_EQ (oid, &oid_Attribute_class);
}

/*
 * oid_compare() - Compare two oids
 *   return: 0 (oid1 == oid2), 1 (oid1 > oid2), -1 (oid1 < oid2)
 *   a(in): Object identifier of first object
 *   b(in): Object identifier of second object
 */
int
oid_compare (const void *a, const void *b)
{
  const OID *oid1_p = (const OID *) a;
  const OID *oid2_p = (const OID *) b;
  int diff;

  diff = oid1_p->volid - oid2_p->volid;
  if (diff)
    {
      return diff;
    }

  diff = oid1_p->pageid - oid2_p->pageid;
  if (diff)
    {
      return diff;
    }

  return oid1_p->slotid - oid2_p->slotid;
}

/*
 * oid_hash() - Hash OIDs
 *   return: hash value
 *   key_oid(in): OID to hash
 *   htsize(in): Size of hash table
 */
unsigned int
oid_hash (const void *key_oid, unsigned int htsize)
{
  unsigned int hash;
  const OID *oid = (OID *) key_oid;

  hash = OID_PSEUDO_KEY (oid);
  return (hash % htsize);
}

/*
 * oid_compare_equals() - Compare oids key for hashing
 *   return:
 *   key_oid1: First key
 *   key_oid2: Second key
 */
int
oid_compare_equals (const void *key_oid1, const void *key_oid2)
{
  const OID *oid1 = (OID *) key_oid1;
  const OID *oid2 = (OID *) key_oid2;

  return OID_EQ (oid1, oid2);
}

/*
 * oid_is_cached_class_oid () - Compare OID with the cached OID identified by
 *				cache_id
 *
 * return :
 * cache_id (in) :
 * oid (in) :
 */
bool
oid_check_cached_class_oid (const int cache_id, const OID * oid)
{
  return OID_EQ (oid, oid_Cache[cache_id].oid);
}

/*
 * oid_set_cached_class_oid () - Sets the cached value of OID
 *
 * cache_id (in) :
 * oid (in) :
 */
void
oid_set_cached_class_oid (const int cache_id, const OID * oid)
{
  COPY_OID (oid_Cache[cache_id].oid, oid);
}

/*
 * oid_get_cached_class_name () - get the name of cached class
 *
 * return   :
 * cache_id (in) :
 */
const char *
oid_get_cached_class_name (const int cache_id)
{
  return oid_Cache[cache_id].class_name;
}

/*
 * oid_is_cached_class_oid () - Used to find a class OID in the cache.
 *			      Currently only used for system classes.
 *
 * return	 : true/false if found
 * class_oid (in): class OID to search for
 */
bool
oid_is_cached_class_oid (const OID * class_oid)
{
  int i;

  for (i = OID_CACHE_ROOT_CLASS_ID; i < OID_CACHE_SIZE; i++)
    {
      if (OID_EQ (oid_Cache[i].oid, class_oid))
	{
	  return true;
	}
    }

  return false;
}

/*
 * oid_get_rep_read_tran_oid () - Get OID that is used for RR transactions
 *				  locking.
 *
 * return    : return the OID.
 */
OID *
oid_get_rep_read_tran_oid (void)
{
  return &oid_Rep_Read_Tran;
}

/*
 * oid_is_system_class () - Check if class identified with class_oid is
 *			      system class.
 *
 * return		   : Error code.
 * class_oid (in)	   : Class object identifier.
 * is_system_class_p (out) : True is class is a system class.
 */
bool
oid_is_system_class (const OID * class_oid)
{
  assert (class_oid != NULL && !OID_ISNULL (class_oid));

  return oid_is_cached_class_oid (class_oid);
}
