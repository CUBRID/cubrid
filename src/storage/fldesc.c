/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * fldes.c - stuffs for types of the file descriptor 
 *
 */

#ident "$Id$"
#include <stdio.h>

#include "fldesc.h"
#include "file_manager.h"
#include "oid.h"
#include "memory_manager_2.h"
#include "heap_file.h"

static void file_descriptor_dump_heap (THREAD_ENTRY * thread_p,
				       const FILE_HEAP_DES * heap_file_des_p);
static void file_descriptor_dump_multi_page_object_heap (const
							 FILE_OVF_HEAP_DES *
							 ovf_hfile_des_p);
static void file_descriptor_dump_btree_overflow_key_file_des (const
							      FILE_OVF_BTREE_DES
							      *
							      btree_ovf_des_p);
static void file_print_name_of_class (THREAD_ENTRY * thread_p,
				      const OID * class_oid_p);
static void file_print_class_name_of_instance (THREAD_ENTRY * thread_p,
					       const OID * inst_oid_p);
static void file_print_name_of_class_with_attrid (THREAD_ENTRY * thread_p,
						  const OID * class_oid_p,
						  const int attr_id);

/*
 * file_descriptor_get_length():
 *
 *   returns: the size of the file descriptor of the given file type.
 *   file_type(IN): file_type
 *
 */
int
file_descriptor_get_length (const FILE_TYPE file_type)
{
  switch (file_type)
    {
    case FILE_TRACKER:
      return 0;
    case FILE_HEAP:
      return sizeof (FILE_HEAP_DES);
    case FILE_MULTIPAGE_OBJECT_HEAP:
      return sizeof (FILE_OVF_HEAP_DES);
    case FILE_BTREE:
      return sizeof (FILE_BTREE_DES);
    case FILE_BTREE_OVERFLOW_KEY:
      return sizeof (FILE_OVF_BTREE_DES);
    case FILE_EXTENDIBLE_HASH:
    case FILE_EXTENDIBLE_HASH_DIRECTORY:
      return sizeof (FILE_EHASH_DES);
    case FILE_LONGDATA:
      return sizeof (FILE_LO_DES);
    case FILE_CATALOG:
    case FILE_QUERY_AREA:
    case FILE_TMP:
    case FILE_TMP_TMP:
    case FILE_EITHER_TMP:
    case FILE_UNKNOWN_TYPE:
    default:
      return 0;
    }
}

/*
 * file_descriptor_dump(): 
 *      dump the file descriptor of the given file type.
 *
 *   returns: none
 *   file_type(IN): file_type
 *   file_des_p(IN): ptr to the file descritor
 *
 */

void
file_descriptor_dump (THREAD_ENTRY * thread_p, const FILE_TYPE file_type,
		      const void *file_des_p)
{
  if (file_des_p == NULL)
    {
      return;
    }

  switch (file_type)
    {
    case FILE_TRACKER:
      break;

    case FILE_HEAP:
      file_descriptor_dump_heap (thread_p,
				 (const FILE_HEAP_DES *) file_des_p);
      break;

    case FILE_MULTIPAGE_OBJECT_HEAP:
      file_descriptor_dump_multi_page_object_heap ((const FILE_OVF_HEAP_DES *)
						   file_des_p);
      break;

    case FILE_BTREE:
      {
	const FILE_BTREE_DES *btree_des_p = (FILE_BTREE_DES *) file_des_p;

	file_print_name_of_class_with_attrid (thread_p,
					      &btree_des_p->class_oid,
					      btree_des_p->attr_id);
	break;
      }

    case FILE_BTREE_OVERFLOW_KEY:
      file_descriptor_dump_btree_overflow_key_file_des ((const
							 FILE_OVF_BTREE_DES *)
							file_des_p);
      break;

    case FILE_EXTENDIBLE_HASH:
    case FILE_EXTENDIBLE_HASH_DIRECTORY:
      {
	const FILE_EHASH_DES *ext_hash_des_p = (FILE_EHASH_DES *) file_des_p;

	file_print_name_of_class_with_attrid (thread_p,
					      &ext_hash_des_p->class_oid,
					      ext_hash_des_p->attr_id);
	break;
      }
    case FILE_LONGDATA:
      {
	const FILE_LO_DES *lo_des_p = (FILE_LO_DES *) file_des_p;

	file_print_class_name_of_instance (thread_p, &lo_des_p->oid);
	break;
      }
    case FILE_CATALOG:
    case FILE_QUERY_AREA:
    case FILE_TMP:
    case FILE_TMP_TMP:
    case FILE_EITHER_TMP:
    case FILE_UNKNOWN_TYPE:
    default:
      fprintf (stdout, "....Don't know how to dump desc..\n");
      break;
    }
}

static void
file_descriptor_dump_heap (THREAD_ENTRY * thread_p,
			   const FILE_HEAP_DES * heap_file_des_p)
{
  file_print_name_of_class (thread_p, &heap_file_des_p->class_oid);
}

static void
file_descriptor_dump_multi_page_object_heap (const FILE_OVF_HEAP_DES *
					     ovf_hfile_des_p)
{
  fprintf (stdout, "Overflow for HFID: %2d|%4d|%4d\n",
	   ovf_hfile_des_p->hfid.vfid.volid,
	   ovf_hfile_des_p->hfid.vfid.fileid, ovf_hfile_des_p->hfid.hpgid);
}

static void
file_descriptor_dump_btree_overflow_key_file_des (const FILE_OVF_BTREE_DES *
						  btree_ovf_des_p)
{
  fprintf (stdout, "Overflow keys for BTID: %2d|%4d|%4d\n",
	   btree_ovf_des_p->btid.vfid.volid,
	   btree_ovf_des_p->btid.vfid.fileid,
	   btree_ovf_des_p->btid.root_pageid);
}

static void
file_print_name_of_class (THREAD_ENTRY * thread_p, const OID * class_oid_p)
{
  char *class_name_p = NULL;

  if (!OID_ISNULL (class_oid_p))
    {
      class_name_p = heap_get_class_name (thread_p, class_oid_p);
      fprintf (stdout, "CLASS_OID:%2d|%4d|%2d (%s)\n",
	       class_oid_p->volid, class_oid_p->pageid, class_oid_p->slotid,
	       (class_name_p) ? class_name_p : "*UNKNOWN-CLASS*");
      if (class_name_p)
	{
	  free_and_init (class_name_p);
	}
    }
  else
    {
      fprintf (stdout, "\n");
    }
}

static void
file_print_class_name_of_instance (THREAD_ENTRY * thread_p,
				   const OID * inst_oid_p)
{
  char *class_name_p = NULL;

  if (!OID_ISNULL (inst_oid_p))
    {
      class_name_p = heap_get_class_name_of_instance (thread_p, inst_oid_p);
      fprintf (stdout, "CLASS_OID:%2d|%4d|%2d (%s)\n",
	       inst_oid_p->volid, inst_oid_p->pageid, inst_oid_p->slotid,
	       (class_name_p) ? class_name_p : "*UNKNOWN-CLASS*");
      if (class_name_p)
	{
	  free_and_init (class_name_p);
	}
    }
  else
    {
      fprintf (stdout, "\n");
    }
}

static void
file_print_name_of_class_with_attrid (THREAD_ENTRY * thread_p,
				      const OID * class_oid_p,
				      const int attr_id)
{
  char *class_name_p = NULL;

  if (!OID_ISNULL (class_oid_p))
    {
      class_name_p = heap_get_class_name (thread_p, class_oid_p);
      fprintf (stdout, "CLASS_OID:%2d|%4d|%2d (%s), ATTRID: %2d\n",
	       class_oid_p->volid, class_oid_p->pageid, class_oid_p->slotid,
	       (class_name_p) ? class_name_p : "*UNKNOWN-CLASS*", attr_id);
      if (class_name_p)
	{
	  free_and_init (class_name_p);
	}
    }
  else
    {
      fprintf (stdout, "\n");
    }
}
