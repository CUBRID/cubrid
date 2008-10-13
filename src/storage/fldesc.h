/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * fldes.h - defines the file descriptors for each file type.
 *
 */

#ifndef _FLDES_H_
#define _FLDES_H_

#ident "$Id$"

#include "oid.h"
#include "file_manager.h"
#include "common.h"

/* Heap file descriptor */
typedef struct file_heap_des FILE_HEAP_DES;
struct file_heap_des
{
  OID class_oid;
};

/* Overflow heap file descriptor */
typedef struct file_ovf_heap_des FILE_OVF_HEAP_DES;
struct file_ovf_heap_des
{
  HFID hfid;
};

/* Btree file descriptor */
typedef struct file_btree_des FILE_BTREE_DES;
struct file_btree_des
{
  OID class_oid;
  int attr_id;
};

/* Overflow key file descriptor */
typedef struct file_ovf_btree_des FILE_OVF_BTREE_DES;
struct file_ovf_btree_des
{
  BTID btid;
};

/* Extendible Hash file descriptor */
typedef struct file_ehash_des FILE_EHASH_DES;
struct file_ehash_des
{
  OID class_oid;
  int attr_id;
};

/* LO file descriptor */
typedef struct file_lo_des FILE_LO_DES;
struct file_lo_des
{
  OID oid;
};

extern int file_descriptor_get_length (const FILE_TYPE file_type);
extern void file_descriptor_dump (THREAD_ENTRY * thread_p,
				  const FILE_TYPE file_type,
				  const void *file_descriptor_p);

#endif /* _FLDES_H_ */
