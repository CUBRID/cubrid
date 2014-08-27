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

extern const OID oid_Null_oid;
extern OID *oid_Root_class_oid;
extern OID *oid_Serial_class_oid;
extern PAGEID oid_Next_tempid;

extern void oid_set_root (const OID * oid);
extern bool oid_is_root (const OID * oid);

extern void oid_set_serial (const OID * oid);
extern bool oid_is_serial (const OID * oid);
extern void oid_get_serial_oid (OID * oid);

extern void oid_set_partition (const OID * oid);
extern bool oid_is_partition (const OID * oid);
extern void oid_get_partition_oid (OID * oid);

extern int oid_compare (const void *oid1, const void *oid2);
extern unsigned int oid_hash (const void *key_oid, unsigned int htsize);
extern int oid_compare_equals (const void *key_oid1, const void *key_oid2);

#endif /* _OID_H_ */
