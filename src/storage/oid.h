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
 * oid.h - OBJECT IDENTIFIER (OID) MODULE  (AT CLIENT AND SERVER)
 */

#ifndef _OID_H_
#define _OID_H_

#ident "$Id$"

#include "storage_common.h"
#include "dbtype.h"

#define ROOTCLASS_NAME "Rootclass"	/* Name of Rootclass */

#define VIRTUAL_CLASS_DIR_OID_MASK (1 << 15)

#define OID_INITIALIZER \
  {NULL_PAGEID, NULL_SLOTID, NULL_VOLID}

#define OID_AS_ARGS(oidp) (oidp)->volid, (oidp)->pageid, (oidp)->slotid

#if !defined(SERVER_MODE)
#define OID_TEMPID_MIN          INT_MIN
#define OID_INIT_TEMPID()       (oid_Next_tempid = NULL_PAGEID)

/* The next tempid will NULL_PAGEID if the next tempid value is underflow */
#define OID_NEXT_TEMPID() \
  ((--oid_Next_tempid <= OID_TEMPID_MIN) ? NULL_PAGEID : oid_Next_tempid)

#define OID_ASSIGN_TEMPOID(oidp) ((oidp)->volid  = NULL_VOLID,          \
				  (oidp)->pageid = OID_NEXT_TEMPID(),   \
				  (oidp)->slotid = - tm_Tran_index)
#endif /* !SERVER_MODE */

#define SET_OID(dest_oid_ptr, vol_id, page_id, slot_id)  \
  do \
    { \
      (dest_oid_ptr)->volid = vol_id; \
      (dest_oid_ptr)->pageid = page_id; \
      (dest_oid_ptr)->slotid = slot_id; \
    } \
  while (0)

#define COPY_OID(dest_oid_ptr, src_oid_ptr) \
  do \
    { \
      *(dest_oid_ptr) = *(src_oid_ptr); \
    } \
  while (0)

#define SAFE_COPY_OID(dest_oid_ptr, src_oid_ptr) \
  if (src_oid_ptr) \
    { \
      *(dest_oid_ptr) = *(src_oid_ptr); \
    } \
  else \
    { \
      OID_SET_NULL (dest_oid_ptr); \
    }

#define OID_ISTEMP(oidp)        ((oidp)->pageid < NULL_PAGEID)
#define OID_IS_ROOTOID(oidp)    (OID_EQ((oidp), oid_Root_class_oid))
#define OID_IS_PSEUDO_OID(oidp) ((oidp)->volid < NULL_VOLID)

#define OID_SET_NULL(oidp) \
  do { \
    (oidp)->pageid = NULL_PAGEID; \
    (oidp)->slotid = NULL_SLOTID; \
    (oidp)->volid = NULL_VOLID; \
  } while(0)

#define OID_EQ(oidp1, oidp2) \
  ((oidp1) == (oidp2) || ((oidp1)->pageid == (oidp2)->pageid && \
			  (oidp1)->slotid == (oidp2)->slotid && \
			  (oidp1)->volid  == (oidp2)->volid))

#define OID_GT(oidp1, oidp2) \
  ((oidp1) != (oidp2) &&						      \
   ((oidp1)->volid > (oidp2)->volid ||					      \
    ((oidp1)->volid == (oidp2)->volid && (oidp1)->pageid > (oidp2)->pageid) ||\
    ((oidp1)->volid == (oidp2)->volid && (oidp1)->pageid == (oidp2)->pageid   \
     && (oidp1)->slotid > (oidp2)->slotid)))

#define OID_GTE(oidp1, oidp2) \
  ((oidp1) == (oidp2) ||						      \
   ((oidp1)->volid > (oidp2)->volid ||					      \
    ((oidp1)->volid == (oidp2)->volid && (oidp1)->pageid > (oidp2)->pageid) ||\
    ((oidp1)->volid == (oidp2)->volid && (oidp1)->pageid == (oidp2)->pageid   \
     && (oidp1)->slotid > (oidp2)->slotid) ||				      \
    ((oidp1)->volid == (oidp2)->volid && (oidp1)->pageid == (oidp2)->pageid   \
     && (oidp1)->slotid == (oidp2)->slotid)))

#define OID_LT(oidp1, oidp2) \
  ((oidp1) != (oidp2) &&						      \
   ((oidp1)->volid < (oidp2)->volid ||					      \
    ((oidp1)->volid == (oidp2)->volid && (oidp1)->pageid < (oidp2)->pageid) ||\
    ((oidp1)->volid == (oidp2)->volid && (oidp1)->pageid == (oidp2)->pageid   \
     && (oidp1)->slotid < (oidp2)->slotid)))

#define OID_LTE(oidp1, oidp2) \
  ((oidp1) == (oidp2) ||						      \
   ((oidp1)->volid < (oidp2)->volid ||					      \
    ((oidp1)->volid == (oidp2)->volid && (oidp1)->pageid < (oidp2)->pageid) ||\
    ((oidp1)->volid == (oidp2)->volid && (oidp1)->pageid == (oidp2)->pageid   \
     && (oidp1)->slotid < (oidp2)->slotid) ||				      \
    ((oidp1)->volid == (oidp2)->volid && (oidp1)->pageid == (oidp2)->pageid   \
     && (oidp1)->slotid == (oidp2)->slotid)))

/* It is used for hashing purposes */
#define OID_PSEUDO_KEY(oidp) \
  ((OID_ISTEMP(oidp)) ? (unsigned int) -((oidp)->pageid) : \
   ((oidp)->slotid | (((unsigned int)(oidp)->pageid) << 8)) ^ \
   ((((unsigned int)(oidp)->pageid) >> 8) | \
    (((unsigned int)(oidp)->volid) << 24)))

#define OID_IS_VIRTUAL_CLASS_OF_DIR_OID(oidp) \
  ((((oidp)->slotid & VIRTUAL_CLASS_DIR_OID_MASK) \
    == VIRTUAL_CLASS_DIR_OID_MASK) ? true : false)

#define OID_GET_VIRTUAL_CLASS_OF_DIR_OID(class_oidp,virtual_oidp) \
  do \
    { \
      (virtual_oidp)->volid = (class_oidp)->volid; \
      (virtual_oidp)->pageid = (class_oidp)->pageid; \
      (virtual_oidp)->slotid = ((class_oidp)->slotid) \
			        | VIRTUAL_CLASS_DIR_OID_MASK; \
    } \
  while (0)

#define OID_GET_REAL_CLASS_OF_DIR_OID(virtual_oidp,class_oidp) \
  do \
    { \
      (class_oidp)->volid = (virtual_oidp)->volid; \
      (class_oidp)->pageid = (virtual_oidp)->pageid; \
      (class_oidp)->slotid = ((virtual_oidp)->slotid) \
			     & (~VIRTUAL_CLASS_DIR_OID_MASK); \
    } \
  while (0)

enum
{
  OID_CACHE_ROOT_CLASS_ID = 0,
  OID_CACHE_CLASS_CLASS_ID,
  OID_CACHE_ATTRIBUTE_CLASS_ID,
  OID_CACHE_DOMAIN_CLASS_ID,
  OID_CACHE_METHOD_CLASS_ID,
  OID_CACHE_METHSIG_CLASS_ID,
  OID_CACHE_METHARG_CLASS_ID,
  OID_CACHE_METHFILE_CLASS_ID,
  OID_CACHE_QUERYSPEC_CLASS_ID,
  OID_CACHE_INDEX_CLASS_ID,
  OID_CACHE_INDEXKEY_CLASS_ID,
  OID_CACHE_DATATYPE_CLASS_ID,
  OID_CACHE_CLASSAUTH_CLASS_ID,
  OID_CACHE_PARTITION_CLASS_ID,
  OID_CACHE_STORED_PROC_CLASS_ID,
  OID_CACHE_STORED_PROC_ARGS_CLASS_ID,
  OID_CACHE_SERIAL_CLASS_ID,
  OID_CACHE_HA_APPLY_INFO_CLASS_ID,
  OID_CACHE_COLLATION_CLASS_ID,
  OID_CACHE_CHARSET_CLASS_ID,
  OID_CACHE_TRIGGER_CLASS_ID,
  OID_CACHE_USER_CLASS_ID,
  OID_CACHE_PASSWORD_CLASS_ID,
  OID_CACHE_AUTH_CLASS_ID,
  OID_CACHE_OLD_ROOT_CLASS_ID,
  OID_CACHE_DB_ROOT_CLASS_ID,

  OID_CACHE_SIZE
};

extern const OID oid_Null_oid;
extern OID *oid_Root_class_oid;
extern OID *oid_Serial_class_oid;
extern OID *oid_User_class_oid;
extern PAGEID oid_Next_tempid;

extern void oid_set_root (const OID * oid);
extern bool oid_is_root (const OID * oid);

extern void oid_set_serial (const OID * oid);
extern bool oid_is_serial (const OID * oid);
extern void oid_get_serial_oid (OID * oid);

extern void oid_set_partition (const OID * oid);
extern bool oid_is_partition (const OID * oid);
extern void oid_get_partition_oid (OID * oid);

extern bool oid_is_db_class (const OID * oid);
extern bool oid_is_db_attribute (const OID * oid);

extern int oid_compare (const void *oid1, const void *oid2);
extern unsigned int oid_hash (const void *key_oid, unsigned int htsize);
extern int oid_compare_equals (const void *key_oid1, const void *key_oid2);
extern bool oid_check_cached_class_oid (const int cache_id, const OID * oid);
extern void oid_set_cached_class_oid (const int cache_id, const OID * oid);
extern const char *oid_get_cached_class_name (const int cache_id);
extern bool oid_is_cached_class_oid (const OID * class_oid);
extern OID *oid_get_rep_read_tran_oid (void);
extern int oid_is_system_class (const OID * class_oid, bool * is_system_class_p);

#define OID_ISNULL(oidp)        ((oidp)->pageid == NULL_PAGEID)

/* From set_object.h */
/*
 * struct setobj
 * The internal structure of a setobj data struct is private to this module.
 * all access to this structure should be encapsulated via function calls.
 */

struct setobj
{

  DB_TYPE coltype;
  int size;			/* valid indexes from 0 to size -1 aka the number of represented values in the
				 * collection */
  int lastinsert;		/* the last value insertion point 0 to size. */
  int topblock;			/* maximum index of an allocated block. This is the maximum non-NULL db_value pointer
				 * index of array. array[topblock] should be non-NULL. array[topblock+1] will be a NULL 
				 * pointer for future expansion. */
  int arraytop;			/* maximum indexable pointer in array the valid indexes for array are 0 to arraytop
				 * inclusive Generally this may be greater than topblock */
  int topblockcount;		/* This is the max index of the top block Since it may be shorter than a standard sized 
				 * block for space efficicency. */
  DB_VALUE **array;

  /* not stored on disk, attached at run time by the schema */
  struct tp_domain *domain;

  /* external reference list */
  DB_COLLECTION *references;

  /* clear if we can't guarentee sort order, always on for sequences */
  unsigned sorted:1;

  /* set if we can't guarentee that there are no temporary OID's in here */
  unsigned may_have_temporary_oids:1;
};

#endif /* _OID_H_ */
