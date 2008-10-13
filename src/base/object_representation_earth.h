/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * orsr.h - Definitions for the server side functions that extract class 
 *          information from disk representations
 */

#ifndef _ORSR_H_
#define _ORSR_H_

#ident "$Id$"

#include "common.h"
#include "system_catalog.h"

/*
 * OR_ATT_BTID_PREALLOC                                                       
 *                                                                            
 *    Optimization for OR_ATTRIBUTE structures.                               
 *    After we've built up a list of OR_ATTRIBUTES, we go through and         
 *    annotate them with index & constraint information by scanning           
 *    over the class property list.  Since an attribute can have a variable   
 *    number of btid's associated with it, the btid array normally grows      
 *    incrementally.  To avoid realloc we keep a static array of           
 *    BTID's directly in the OR_ATTRIBUTE structure.  This array will be used 
 *    until the number of btid's goes over the threashold after which         
 *    we'll start allocating them with malloc.  This allows us to avoid       
 *    memory management for the usual case where we have just one or two      
 *    btids for the attribute.                                                
 */
#define OR_ATT_BTID_PREALLOC 8

/*
 * OR_ATTRIBUTE                                                             
 *                                                                            
 *    Server side memory representation of an attribute definition.           
 *    Built from the disk representation of a class.                          
 *    Part of the OR_CLASSREP structure hierarchy.                            
 */

typedef struct or_attribute OR_ATTRIBUTE;
struct or_attribute
{
  OR_ATTRIBUTE *next;

  int id;			/* unique id */
  DB_TYPE type;			/* basic type */
  int location;			/* fixed offset or variable table index */
  int position;			/* storage position (list index) */
  OID classoid;			/* source class object id */

  /* could this be converted to a server side DB_VALUE ? */
  int val_length;		/* default value lenght */
  void *value;			/* default value */

  BTID index;			/* btree id if indexed */

  int n_btids;			/* Number of ID's in the btids array */
  BTID *btids;			/* B-tree ID's for indexes and constraints */

  unsigned is_fixed:1;		/* non-zero if this is a fixed width attribute */

  TP_DOMAIN *domain;		/* full domain of this attribute */

  /* local array of btid's to use  if possible */
  int max_btids;		/* Size of the btids array */
  BTID btid_pack[OR_ATT_BTID_PREALLOC];

  unsigned is_autoincrement:1;	/* non-zero if att is auto increment att */
  OID serial_obj;		/* db_serial's instance */
};

typedef enum
{
  BTREE_UNIQUE,
  BTREE_INDEX,
  BTREE_REVERSE_UNIQUE,
  BTREE_REVERSE_INDEX,
  BTREE_PRIMARY_KEY,
  BTREE_FOREIGN_KEY
} BTREE_TYPE;

typedef struct or_foreign_key OR_FOREIGN_KEY;
struct or_foreign_key
{
  OR_FOREIGN_KEY *next;		/* for pk */
  OID ref_class_oid;
  BTID ref_class_pk_btid;
  OID self_oid;
  BTID self_btid;
  int del_action;
  int upd_action;
  bool is_cache_obj;
  int cache_attr_id;
  char *name;
};

/*
 * OR_INDEX                                                                   
 *                                                                            
 *    Server side representation of the indexes associated with a class.      
 *    The index may be associated with an INDEXed attribute, or an attribute  
 *    with a UNIQUE constraint.  The index may be multi-column and will       
 *    point to the member attributes.  This is build from the disk            
 *    representation of a class and is part of the OR_CLASSREP structure      
 *    hierarchy.                                                              
 */
typedef struct or_index OR_INDEX;
struct or_index
{
  OR_INDEX *next;
  BTID btid;			/* btree ID */
  BTREE_TYPE type;		/* btree type */
  int n_atts;			/* Number of associated attributes */
  OR_ATTRIBUTE **atts;		/* Array of associated attributes */
  OR_FOREIGN_KEY *fk;
};

/*
 * OR_CLASSREP                                                                
 *                                                                            
 * description:                                                               
 *    Server side memory representation of a class "representation".          
 *    This is basically a just a list of the attributes defined for           
 *    that representation.                                                    
 *    Built from the disk image of the class, part of the OR_CLASS structure  
 *    hierarchy.                                                              
 *    The attributes in the list will be ordered in "storage" order with      
 *    the fixed attributes first followed by the variable attributes.         
 */

typedef struct or_classrep OR_CLASSREP;
struct or_classrep
{
  OR_CLASSREP *next;

  REPR_ID id;			/* representation id */

  int fixed_length;		/* total size of the fixed width attributes */
  int n_attributes;		/* size of attribute array */
  int n_variable;		/* number of variable width attributes */
  int n_shared_attrs;		/* number of shared attributes */
  int n_class_attrs;		/* number of class attributes */
  int n_indexes;		/* number of indexes */

  OR_ATTRIBUTE *attributes;	/* list of attributes */
  OR_ATTRIBUTE *shared_attrs;	/* list of shared attributes */
  OR_ATTRIBUTE *class_attrs;	/* list of class attributes */
  OR_INDEX *indexes;		/* list of BTIDs for this class */

  unsigned needs_indexes:1;	/* flag indicating if indexes were not loaded */
};

/*
 * OR_CLASS                                                                   
 *                                                                            
 * description:                                                               
 *    Server side memory representation for a class definition.               
 *    Built from the disk representation of the class.                        
 *    The representations list is ordered with the most recent representation 
 *    first and the oldest representation last.                               
 *                                                                            
 *    NOTE: This structure isn't currently used but it may be usefull         
 *    in the future when more sophisticated class information is necessary    
 *    for server side methods.                                                
 */

typedef struct or_class OR_CLASS;
struct or_class
{
  int n_superclasses;
  OID *superclasses;

  int n_subclasses;
  OID *subclasses;

  OR_CLASSREP *representations;

  OID statistics;		/* object containing statistics */
};

extern int or_class_repid (RECDES * record);
extern void or_class_hfid (RECDES * record, HFID * hfid);
extern void or_class_statistics (RECDES * record, OID * oid);
extern int or_class_subclasses (RECDES * record,
				int *array_size, OID ** array_ptr);
extern int or_get_unique_hierarchy (THREAD_ENTRY * thread_p, RECDES * record,
				    int attrid,
				    BTID * btid,
				    OID ** class_oids,
				    HFID ** hfids, int *num_heaps);
extern OR_CLASSREP *or_get_classrep (RECDES * record, int repid);
extern OR_CLASSREP *or_get_classrep_noindex (RECDES * record, int repid);
extern int or_classrep_needs_indexes (OR_CLASSREP * rep);
extern OR_CLASSREP *or_classrep_load_indexes (OR_CLASSREP * rep,
					      RECDES * record);
extern void or_free_classrep (OR_CLASSREP * rep);
extern const char *or_get_attrname (RECDES * record, int attrid);
extern OR_CLASS *or_get_class (RECDES * record);
extern void or_free_class (OR_CLASS * class_);

/* OLD STYLE INTERFACE */
extern int orc_class_repid (RECDES * record);
extern void orc_class_hfid_from_record (RECDES * record, HFID * hfid);
extern DISK_REPR *orc_diskrep_from_record (THREAD_ENTRY * thread_p,
					   RECDES * record);
extern void orc_free_diskrep (DISK_REPR * rep);
extern CLS_INFO *orc_class_info_from_record (RECDES * record);
extern void orc_free_class_info (CLS_INFO * info);
extern int orc_subclasses_from_record (RECDES * record,
				       int *array_size, OID ** array_ptr);
#endif /* _ORSR_H_ */
