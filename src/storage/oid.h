/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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

#define COPY_OID(oid_ptr1, oid_ptr2) *(oid_ptr1) = *(oid_ptr2)

#define OID_ISTEMP(oidp)        ((oidp)->pageid < NULL_PAGEID)
#define OID_ISNULL(oidp)        ((oidp)->pageid == NULL_PAGEID)
#define OID_IS_ROOTOID(oidp)    (OID_EQ((oidp), oid_Root_class_oid))

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
extern PAGEID oid_Next_tempid;

extern void oid_set_root (const OID * oid);
extern bool oid_is_root (const OID * oid);
extern int oid_compare (const void *oid1, const void *oid2);
extern unsigned int oid_hash (const void *key_oid, unsigned int htsize);
extern int oid_compare_equals (const void *key_oid1, const void *key_oid2);

#endif /* _OID_H_ */
