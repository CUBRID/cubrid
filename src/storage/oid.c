/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * oid.c - OBJECT IDENTIFIER (OID) MODULE  (AT CLIENT AND SERVER)
 */

#ident "$Id$"

#include "config.h"

#include "oid.h"

static OID oid_Root_class = { 0, 0, 0 };

const OID oid_Null_oid = { NULL_PAGEID, NULL_SLOTID, NULL_VOLID };
PAGEID oid_Next_tempid = NULL_PAGEID;

/* ROOT_CLASS OID values. Set during restart/initialization.*/
OID *oid_Root_class_oid = &oid_Root_class;

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
 * oid_compare() - Compare two oids
 *   return: 0 (oid1 == oid2), 1 (oid1 > oid2), -1 (oid1 < oid2)
 *   a(in): Object identifier of first object
 *   b(in): Object identifier of second object
 */
int
oid_compare (const void *a, const void *b)
{
  const OID *oid1 = (const OID *) a;
  const OID *oid2 = (const OID *) b;

  if (oid1 == oid2)
    {
      return 0;
    }

  if (oid1->volid > oid2->volid)
    {
      return 1;
    }
  if (oid1->volid < oid2->volid)
    {
      return -1;
    }

  if (oid1->pageid > oid2->pageid)
    {
      return 1;
    }
  if (oid1->pageid < oid2->pageid)
    {
      return -1;
    }

  if (oid1->slotid > oid2->slotid)
    {
      return 1;
    }
  if (oid1->slotid < oid2->slotid)
    {
      return -1;
    }

  return 0;
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
