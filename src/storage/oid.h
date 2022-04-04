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
 * oid.h - OBJECT IDENTIFIER (OID) MODULE  (AT CLIENT AND SERVER)
 */

#ifndef _OID_H_
#define _OID_H_

#ident "$Id$"

#include "storage_common.h"
#include "dbtype_def.h"
#ifdef __cplusplus
#include <functional>
#endif

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
#define OID_ISNULL(oidp)        ((oidp)->pageid == NULL_PAGEID)
#define OID_IS_ROOTOID(oidp)    (OID_EQ((oidp), oid_Root_class_oid))
#define OID_IS_PSEUDO_OID(oidp) ((oidp)->volid < NULL_VOLID)

#define OID_SET_NULL(oidp) \
  do { \
    (oidp)->pageid = NULL_PAGEID; \
    (oidp)->slotid = NULL_SLOTID; \
    (oidp)->volid = NULL_VOLID; \
  } while(0)

#define OID_EQ(oidp1, oidp2) \
  ( (oidp1)->pageid == (oidp2)->pageid && \
    (oidp1)->slotid == (oidp2)->slotid && \
    (oidp1)->volid  == (oidp2)->volid )

#define OID_GT(oidp1, oidp2) \
   ( ((oidp1)->volid != (oidp2)->volid) ? ((oidp1)->volid > (oidp2)->volid)          \
        : ((oidp1)->pageid != (oidp2)->pageid) ? ((oidp1)->pageid > (oidp2)->pageid) \
        : ((oidp1)->slotid > (oidp2)->slotid) )

#define OID_GTE(oidp1, oidp2) \
   ( ((oidp1)->volid != (oidp2)->volid) ? ((oidp1)->volid > (oidp2)->volid)          \
        : ((oidp1)->pageid != (oidp2)->pageid) ? ((oidp1)->pageid > (oidp2)->pageid) \
        : ((oidp1)->slotid >= (oidp2)->slotid) )

#define OID_LT(oidp1, oidp2) \
   ( ((oidp1)->volid != (oidp2)->volid) ? ((oidp1)->volid < (oidp2)->volid)          \
        : ((oidp1)->pageid != (oidp2)->pageid) ? ((oidp1)->pageid < (oidp2)->pageid) \
        : ((oidp1)->slotid < (oidp2)->slotid) )

#define OID_LTE(oidp1, oidp2) \
   ( ((oidp1)->volid != (oidp2)->volid) ? ((oidp1)->volid < (oidp2)->volid)          \
        : ((oidp1)->pageid != (oidp2)->pageid) ? ((oidp1)->pageid < (oidp2)->pageid) \
        : ((oidp1)->slotid <= (oidp2)->slotid) )

/* It is used for hashing purposes */
#define OID_PSEUDO_KEY(oidp) \
  ((OID_ISTEMP(oidp)) ? (unsigned int) -((oidp)->pageid) : \
   ((oidp)->slotid | (((unsigned int)(oidp)->pageid) << 8)) ^ \
   ((((unsigned int)(oidp)->pageid) >> 8) | \
    (((unsigned int)(oidp)->volid) << 24)))

#ifdef __cplusplus
// *INDENT-OFF*
template <>
struct std::hash<OID>
{
  size_t operator()(const OID& oid) const
  {
    return OID_PSEUDO_KEY (&oid);
  }
};

inline bool operator==(const OID& oid1, const OID& oid2)
{
  return OID_EQ (&oid1, &oid2);
}

inline bool operator!=(const OID& oid1, const OID& oid2)
{
  return !OID_EQ (&oid1, &oid2);
}
// *INDENT-ON*
#endif

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
  OID_CACHE_DB_SERVER_CLASS_ID,

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
extern bool oid_is_system_class (const OID * class_oid);
#endif /* _OID_H_ */
