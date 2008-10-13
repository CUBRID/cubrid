/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 * 
 * cat.h : Catalog manager interface header file
 */

#ifndef _CAT_H_
#define _CAT_H_

#ident "$Id$"

#include "config.h"

#include "error_manager.h"
#include "log.h"
#include "memory_manager_2.h"
#include "statistics.h"
#include "disk_manager.h"
#include "object_representation.h"
#include "common.h"

#define NULL_REPRID       -1	/* Null Representation Identifier */
#define NULL_ATTRID       -1	/* Null Attribute Identifier */

typedef struct ctid CTID;
struct ctid
{
  VFID vfid;			/* catalog volume identifier */
  EHID xhid;			/* extendible hash index identifier */
  PAGEID hpgid;			/* catalog header page identifier */
};				/* catalog identifier */

typedef int REPR_ID;		/* representation identifier */
typedef int ATTR_ID;		/* attribute identifier */

/*
 * disk_representation                                                
 *                                                                          
 * This is the primary communication structure between the schema manager   
 * and the catalog manager.   It contains information on the order and      
 * location of attributes for a particular class of object.		    
 */

typedef struct disk_representation DISK_REPR;
struct disk_representation
{
  REPR_ID id;			/* representation identifier  */
  int n_fixed;			/* number of fixed attributes */
  struct disk_attribute *fixed;	/* fixed attribute structures */
  int fixed_length;		/* total length of fixed attributes */
  int n_variable;		/* number of variable attributes */
  struct disk_attribute *variable;	/* variable attribute structures */
  int num_objects;		/* number of belonging objects  */
};				/* object disk representation */


/*
 * disk_attribute                                                     
 *                                                                          
 * This contains information about attributes as the exist on disk.	    
 * This is one of the communication structures between the schema manager   
 * and the catalog manager.					  	    
 * For fixed field attributes, the location field will contain a byte       
 * offset.  For variable field attributes, the location field will contain  
 * an index into the variable field information block in the disk object.   
 */

typedef struct disk_attribute DISK_ATTR;
struct disk_attribute
{
  ATTR_ID id;			/* attribute identifier */
  int location;			/* location in disk representation
				 * exact offset if fixed attr.
				 * index to offset table if var attr.*/
  DB_TYPE type;			/* datatype */
  int val_length;		/* default value length >= 0 */
  void *value;			/* default value */
  int position;			/* storage position (fixed attributes only) */
  OID classoid;			/* source class object id */
  DB_DATA min_value;		/* minimum existing value */
  DB_DATA max_value;		/* maximum existing value */
  int n_btstats;		/* number of B+tree statistics information */
  BTREE_STATS *bt_stats;	/* pointer to array of BTREE_STATS;
				 * BTREE_STATS[n_btstats] */
};				/* disk attribute structure */

typedef struct cls_info CLS_INFO;
struct cls_info
{
  HFID hfid;			/* heap file identifier for the class */
  int tot_pages;		/* total number of pages in the heap file */
  int tot_objects;		/* total number of objects for this class */
  unsigned int time_stamp;	/* timestamp of last update */
};				/* class specific information */

extern CTID catalog_Id;		/* global catalog identifier */

#if defined(SERVER_MODE)
/* in xserver.h */
extern int xcatalog_is_acceptable_new_representation (THREAD_ENTRY * thread_p,
						      OID * class_id,
						      HFID * hfid,
						      int *can_accept);
#endif /* SERVER_MODE */

extern void catalog_free_disk_attribute (DISK_ATTR * atr);
extern void catalog_free_representation (DISK_REPR * dr);
extern void catalog_free_class_info (CLS_INFO * Cls_Info);
extern void catalog_initialize (CTID * catid);
extern void catalog_finalize (void);

/* these two routines should be called only once and by the root */
extern CTID *catalog_create (THREAD_ENTRY * thread_p, CTID * catid,
			     DKNPAGES exp_ncatpg, DKNPAGES exp_nindpg);
extern int catalog_destroy (void);

extern int catalog_reclaim_space (THREAD_ENTRY * thread_p);
extern int catalog_add_representation (THREAD_ENTRY * thread_p,
				       OID * class_id, REPR_ID repr_id,
				       DISK_REPR * Disk_Repr);
extern int catalog_add_class_info (THREAD_ENTRY * thread_p, OID * class_id,
				   CLS_INFO * Cls_Info);
extern CLS_INFO *catalog_update_class_info (THREAD_ENTRY * thread_p,
					    OID * class_id,
					    CLS_INFO * cls_info);
extern int catalog_drop_old_representations (THREAD_ENTRY * thread_p,
					     OID * class_id);
extern DISK_REPR *catalog_get_representation (THREAD_ENTRY * thread_p,
					      OID * class_id,
					      REPR_ID repr_id);
extern CLS_INFO *catalog_get_class_info (THREAD_ENTRY * thread_p,
					 OID * class_id);
extern int catalog_get_representation_directory (THREAD_ENTRY * thread_p,
						 OID * class_id,
						 REPR_ID ** reprid_set,
						 int *repr_cnt);
extern int catalog_get_last_representation_id (THREAD_ENTRY * thread_p,
					       OID * cls_oid,
					       REPR_ID * repr_id);
extern int catalog_insert (THREAD_ENTRY * thread_p, RECDES * record,
			   OID * classoid);
extern int catalog_update (THREAD_ENTRY * thread_p, RECDES * record,
			   OID * classoid);
extern int catalog_delete (THREAD_ENTRY * thread_p, OID * classoid);

/* Checkdb consistency check routines */
extern DISK_ISVALID catalog_check_consistency (THREAD_ENTRY * thread_p);

/* Dump routines */
extern void catalog_dump (THREAD_ENTRY * thread_p, int dump_flg);

/* Recovery routines */
extern int catalog_rv_new_page_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
extern int catalog_rv_insert_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
extern int catalog_rv_insert_undo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
extern int catalog_rv_delete_redo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
extern int catalog_rv_delete_undo (THREAD_ENTRY * thread_p, LOG_RCV * recv);
extern int catalog_rv_update (THREAD_ENTRY * thread_p, LOG_RCV * recv);
extern int catalog_rv_ovf_page_logical_insert_undo (THREAD_ENTRY * thread_p,
						    LOG_RCV * recv);

#endif /* _CAT_H_ */
