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
 * locator.c - Transaction object locator (Both client & server)
 */

#ident "$Id$"

#include "locator.h"

#include "config.h"
#include "porting.h"
#include "memory_alloc.h"
#include "oid.h"
#include "object_representation.h"
#include "error_manager.h"
#include "storage_common.h"
#if defined(SERVER_MODE)
#include "connection_error.h"
#endif /* SERVER_MODE */
#include "thread_compat.hpp"
#if defined(SERVER_MODE)
#include "thread_manager.hpp"	// for thread_get_thread_entry_info
#endif /* SERVER_MODE */

#include <stdio.h>
#include <string.h>
#include <assert.h>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

#if !defined(SERVER_MODE)
#define pthread_mutex_init(a, b)
#define pthread_mutex_destroy(a)
#define pthread_mutex_lock(a)	0
#define pthread_mutex_unlock(a)
static int rv;
#endif /* !SERVER_MODE */

#if defined(SERVER_MODE)
#define LOCATOR_NKEEP_LIMIT (50)
#else /* SERVER_MODE */
#define LOCATOR_NKEEP_LIMIT (2)
#endif /* SERVER_MODE */

#define LOCATOR_CACHED_COPYAREA_SIZE_LIMIT \
  (IO_MAX_PAGE_SIZE * 2 + sizeof (LC_COPYAREA))

typedef struct locator_global LOCATOR_GLOBAL;
struct locator_global
{
  struct locator_global_copyareas
  {
    int number;			/* Num of copy areas that has been kept */
    LC_COPYAREA *areas[LOCATOR_NKEEP_LIMIT];	/* Array of free copy areas */
#if defined(SERVER_MODE)
    pthread_mutex_t lock;
#endif				/* SERVER_MODE */
  } copy_areas;

  struct locator_global_lockset_areas
  {
    int number;			/* Num of requested areas that has been kept */
    LC_LOCKSET *areas[LOCATOR_NKEEP_LIMIT];	/* Array of free lockset areas */
#if defined(SERVER_MODE)
    pthread_mutex_t lock;
#endif				/* SERVER_MODE */
  } lockset_areas;

  struct locator_global_lockhint_areas
  {
    int number;			/* Num of lockhinted areas that has been kept */
    LC_LOCKHINT *areas[LOCATOR_NKEEP_LIMIT];	/* Array of free lockhinted */
#if defined(SERVER_MODE)
    pthread_mutex_t lock;
#endif				/* SERVER_MODE */
  } lockhint_areas;

  struct locator_global_packed_areas
  {
    int number;			/* Num of packed areas that have been kept */
    LC_COPYAREA *areas[LOCATOR_NKEEP_LIMIT];	/* Array of free packed areas */
#if defined(SERVER_MODE)
    pthread_mutex_t lock;
#endif				/* SERVER_MODE */
  } packed_areas;
};

static LOCATOR_GLOBAL locator_Keep;

static LC_COPYAREA packed_req_area_ptrs[LOCATOR_NKEEP_LIMIT];

static bool locator_Is_initialized = false;

static char *locator_allocate_packed (int packed_size);
static char *locator_reallocate_packed (char *packed, int packed_size);
static void locator_free_packed (char *packed_area, int packed_size);
#if defined(CUBRID_DEBUG)
static void locator_dump_string (FILE * out_fp, char *dump_string, int length);
static void locator_dump_copy_area_one_object (FILE * out_fp, LC_COPYAREA_ONEOBJ * obj, int obj_index,
					       const LC_COPYAREA * copyarea, int print_rec);
#endif
static int locator_initialize_lockset (LC_LOCKSET * lockset, int length, int max_reqobjs, LOCK reqobj_inst_lock,
				       LOCK reqobj_class_lock, int quit_on_errors);
#if defined(CUBRID_DEBUG)
static void locator_dump_lockset_area_info (FILE * out_fp, LC_LOCKSET * lockset);
static void locator_dump_lockset_classes (FILE * out_fp, LC_LOCKSET * lockset);
static void locator_dump_lockset_objects (FILE * out_fp, LC_LOCKSET * lockset);
#endif
static char *locator_pack_lockset_header (char *packed, LC_LOCKSET * lockset);
static char *locator_pack_lockset_classes (char *packed, LC_LOCKSET * lockset);
static char *locator_pack_lockset_objects (char *packed, LC_LOCKSET * lockset);
static char *locator_unpack_lockset_header (char *unpacked, LC_LOCKSET * lockset);
static char *locator_unpack_lockset_classes (char *unpacked, LC_LOCKSET * lockset);
static char *locator_unpack_lockset_objects (char *unpacked, LC_LOCKSET * lockset);
static int locator_initialize_lockhint (LC_LOCKHINT * lockhint, int length, int max_classes, bool quit_on_errors);
#if defined(CUBRID_DEBUG)
static void locator_dump_lockhint_info (FILE * out_fp, LC_LOCKHINT * lockhint);
static void locator_dump_lockhint_classes (FILE * out_fp, LC_LOCKHINT * lockhint);
#endif
static char *locator_pack_lockhint_header (char *packed, LC_LOCKHINT * lockhint);
static char *locator_pack_lockhint_classes (char *packed, LC_LOCKHINT * lockhint);
static char *locator_unpack_lockhint_header (char *unpacked, LC_LOCKHINT * lockhint);
static char *locator_unpack_lockhint_classes (char *unpacked, LC_LOCKHINT * lockhint);
static bool locator_is_hfid_equal (HFID * hfid1_p, HFID * hfid2_p);

/*
 * locator_is_hfid_equal:
 *
 * return:  bool
 *
 *   hfid1_p(in):
 *   hfid2_p(in):
 *
 * NOTE:
 */
static bool
locator_is_hfid_equal (HFID * hfid1_p, HFID * hfid2_p)
{
  return (hfid1_p->vfid.fileid == hfid2_p->vfid.fileid && hfid1_p->vfid.volid == hfid2_p->vfid.volid
	  && hfid1_p->hpgid == hfid2_p->hpgid);
}

/*
 * locator_initialize_areas: initialize cache areas
 *
 * return:  nothing
 *
 * NOTE: Initialize all areas.
 */
void
locator_initialize_areas (void)
{
  int i;

  if (locator_Is_initialized)
    {
      return;
    }

  locator_Keep.copy_areas.number = 0;
  locator_Keep.lockset_areas.number = 0;
  locator_Keep.lockhint_areas.number = 0;
  locator_Keep.packed_areas.number = 0;

#if defined(SERVER_MODE)
  pthread_mutex_init (&locator_Keep.copy_areas.lock, NULL);
  pthread_mutex_init (&locator_Keep.lockset_areas.lock, NULL);
  pthread_mutex_init (&locator_Keep.lockhint_areas.lock, NULL);
  pthread_mutex_init (&locator_Keep.packed_areas.lock, NULL);
#endif /* SERVER_MODE */

  for (i = 0; i < LOCATOR_NKEEP_LIMIT; i++)
    {
      locator_Keep.copy_areas.areas[i] = NULL;
      locator_Keep.lockset_areas.areas[i] = NULL;
      locator_Keep.lockhint_areas.areas[i] = NULL;
      locator_Keep.packed_areas.areas[i] = &packed_req_area_ptrs[i];
    }

  locator_Is_initialized = true;
}

/*
 * locator_free_areas: Free cached areas
 *
 * return: nothing
 *
 * NOTE: Free all areas that has been cached.
 */
void
locator_free_areas (void)
{
  int i;

  if (locator_Is_initialized == false)
    {
      return;
    }

  for (i = 0; i < locator_Keep.copy_areas.number; i++)
    {
      free_and_init (locator_Keep.copy_areas.areas[i]);
    }

  for (i = 0; i < locator_Keep.lockset_areas.number; i++)
    {
      if (locator_Keep.lockset_areas.areas[i]->packed)
	{
	  free_and_init (locator_Keep.lockset_areas.areas[i]->packed);
	}
      free_and_init (locator_Keep.lockset_areas.areas[i]);
    }

  for (i = 0; i < locator_Keep.lockhint_areas.number; i++)
    {
      if (locator_Keep.lockhint_areas.areas[i]->packed)
	{
	  free_and_init (locator_Keep.lockhint_areas.areas[i]->packed);
	}
      free_and_init (locator_Keep.lockhint_areas.areas[i]);
    }

  for (i = 0; i < locator_Keep.packed_areas.number; i++)
    {
      free_and_init (locator_Keep.packed_areas.areas[i]->mem);
    }

  locator_Keep.copy_areas.number = 0;
  locator_Keep.lockset_areas.number = 0;
  locator_Keep.lockhint_areas.number = 0;
  locator_Keep.packed_areas.number = 0;

#if defined(SERVER_MODE)
  pthread_mutex_destroy (&locator_Keep.copy_areas.lock);
  pthread_mutex_destroy (&locator_Keep.lockset_areas.lock);
  pthread_mutex_destroy (&locator_Keep.lockhint_areas.lock);
  pthread_mutex_destroy (&locator_Keep.packed_areas.lock);
#endif /* SERVER_MODE */

  locator_Is_initialized = false;
}

/*
 *
 *       		     FETCH/FLUSH COPY AREA
 *
 */

/*
 * locator_allocate_packed: allocate an area to pack stuff
 *
 * return:  char * (pack area)
 *
 *   packed_size(in): Packed size needed
 *
 * NOTE: Allocate an area to pack the stuff such as lockset area
 *              which is sent over the network.  The caller needs to free the
 *              packed area.
 */
static char *
locator_allocate_packed (int packed_size)
{
  char *packed_area = NULL;
  int i, tail;
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  rv = pthread_mutex_lock (&locator_Keep.packed_areas.lock);

  for (i = 0; i < locator_Keep.packed_areas.number; i++)
    {
      if (locator_Keep.packed_areas.areas[i]->length >= packed_size)
	{
	  /*
	   * Make sure that the caller is not assuming that the area is
	   * initialized to zeros. That is, make sure caller initialize the area
	   */
	  MEM_REGION_SCRAMBLE (locator_Keep.packed_areas.areas[i]->mem, locator_Keep.packed_areas.areas[i]->length);

	  packed_area = locator_Keep.packed_areas.areas[i]->mem;
	  packed_size = locator_Keep.packed_areas.areas[i]->length;

	  locator_Keep.packed_areas.number--;

	  /* Move the tail to current location */
	  tail = locator_Keep.packed_areas.number;

	  locator_Keep.packed_areas.areas[i]->mem = locator_Keep.packed_areas.areas[tail]->mem;
	  locator_Keep.packed_areas.areas[i]->length = locator_Keep.packed_areas.areas[tail]->length;

	  break;
	}
    }

  pthread_mutex_unlock (&locator_Keep.packed_areas.lock);

  if (packed_area == NULL)
    {
      packed_area = (char *) malloc (packed_size);
    }

  return packed_area;
}

/*
 * locator_reallocate_packed: Reallocate an area to pack stuff
 *
 * return: char * (pack area)
 *
 *   packed(in): Packed pointer
 *   packed_size(in): New size
 *
 * NOTE: Reallocate the given packed area with given size.
 */
static char *
locator_reallocate_packed (char *packed, int packed_size)
{
  return (char *) realloc (packed, packed_size);
}

/*
 * locator_free_packed: Free a packed area
 *
 * return: nothing
 *
 *   packed_area(in): Area to free
 *   packed_size(in): Size of area to free
 *
 * NOTE: Free the given packed area
 */
static void
locator_free_packed (char *packed_area, int packed_size)
{
  int tail;
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  rv = pthread_mutex_lock (&locator_Keep.packed_areas.lock);

  if (locator_Keep.packed_areas.number < LOCATOR_NKEEP_LIMIT)
    {
      tail = locator_Keep.packed_areas.number;

      locator_Keep.packed_areas.areas[tail]->mem = packed_area;
      locator_Keep.packed_areas.areas[tail]->length = packed_size;

      /*
       * Scramble the memory, so that the developer detects invalid references
       * to free'd areas
       */
      MEM_REGION_SCRAMBLE (locator_Keep.packed_areas.areas[tail]->mem, locator_Keep.packed_areas.areas[tail]->length);
      locator_Keep.packed_areas.number++;
    }
  else
    {
      free_and_init (packed_area);
    }

  pthread_mutex_unlock (&locator_Keep.packed_areas.lock);
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * locator_allocate_copyarea; Allocate a copy area for fetching and flushing
 *
 * return: LC_COPYAREA *
 *
 *   npages(in):  Number of needed pages
 *
 * NOTE: Allocate a flush/fetch area of the given size.
 */
LC_COPYAREA *
locator_allocate_copyarea (DKNPAGES npages)
{
  return locator_allocate_copy_area_by_length (npages * IO_PAGESIZE);
}
#endif

/*
 * locator_allocate_copy_area_by_length: Allocate a copy area for
 *                              fetching and flushing purposes.
 *
 * return: LC_COPYAREA *
 *
 *   min_length(in):Length of the copy area
 *
 * NOTE: Allocate a flush/fetch area of the given length.
 */
LC_COPYAREA *
locator_allocate_copy_area_by_length (int min_length)
{
  LC_COPYAREA *copyarea = NULL;
  int network_pagesize;
  int i;
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  /*
   * Make the min_length to be multiple of NETWORK_PAGESIZE since the
   * copyareas are used to copy objects to/from server and we would like to
   * maximize the communication line.
   */
  network_pagesize = db_network_page_size ();

  min_length = DB_ALIGN (min_length, network_pagesize);

  /*
   * Do we have an area of given or larger length cached ?
   */

  rv = pthread_mutex_lock (&locator_Keep.copy_areas.lock);

  for (i = 0; i < locator_Keep.copy_areas.number; i++)
    {
      if (locator_Keep.copy_areas.areas[i]->length >= min_length)
	{
	  copyarea = locator_Keep.copy_areas.areas[i];
	  locator_Keep.copy_areas.areas[i] = locator_Keep.copy_areas.areas[--locator_Keep.copy_areas.number];
	  min_length = copyarea->length;
	  /*
	   * Make sure that the caller is not assuming that the area is
	   * initialized to zeros. That is, make sure caller initialize the area
	   */
	  MEM_REGION_SCRAMBLE (copyarea, copyarea->length);
	  break;
	}
    }

  pthread_mutex_unlock (&locator_Keep.copy_areas.lock);

  if (copyarea == NULL)
    {
      copyarea = (LC_COPYAREA *) malloc (min_length + sizeof (*copyarea));
      if (copyarea == NULL)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
		  (size_t) (min_length + sizeof (*copyarea)));
	  return NULL;
	}
    }

  copyarea->mem = (char *) copyarea + sizeof (*copyarea);
  copyarea->length = min_length;

  return copyarea;
}

LC_COPYAREA *
locator_reallocate_copy_area_by_length (LC_COPYAREA * old_area, int new_length)
{
  LC_COPYAREA_MANYOBJS *old_mobjs, *new_mobjs;
  LC_COPYAREA_ONEOBJ *old_obj, *new_obj;
  LC_COPYAREA *new_area = NULL;
  int i, last_obj_offset = -1;
  int last_obj_length = 0;
  int old_content_length = 0;

  if (old_area == NULL)
    {
      return NULL;
    }

  old_mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (old_area);

  if (new_length < old_area->length)
    {
      return NULL;
    }

  new_area = locator_allocate_copy_area_by_length (new_length);
  if (new_area == NULL)
    {
      return NULL;
    }

  new_mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (new_area);
  new_mobjs->num_objs = old_mobjs->num_objs;
  new_mobjs->multi_update_flags = old_mobjs->multi_update_flags;

  for (i = 0; i < old_mobjs->num_objs; i++)
    {
      old_obj = LC_FIND_ONEOBJ_PTR_IN_COPYAREA (old_mobjs, i);
      new_obj = LC_FIND_ONEOBJ_PTR_IN_COPYAREA (new_mobjs, i);

      LC_COPY_ONEOBJ (new_obj, old_obj);

      if (old_obj->offset > last_obj_offset)
	{
	  last_obj_offset = old_obj->offset;
	  last_obj_length = old_obj->length;
	}
    }

  if (last_obj_offset != -1)
    {
      old_content_length = last_obj_offset + last_obj_length;
    }

  memcpy (new_area->mem, old_area->mem, old_content_length);

  locator_free_copy_area (old_area);

  return new_area;
}

/*
 * locator_free_copy_area: Free a copy area
 *
 * return:
 *
 *   copyarea(in): Area to free
 *
 * NOTE: Free the given copy area.
 */
void
locator_free_copy_area (LC_COPYAREA * copyarea)
{
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  if (LOCATOR_CACHED_COPYAREA_SIZE_LIMIT < (size_t) copyarea->length)
    {
      free_and_init (copyarea);
      return;
    }

  rv = pthread_mutex_lock (&locator_Keep.copy_areas.lock);
  if (locator_Keep.copy_areas.number < LOCATOR_NKEEP_LIMIT)
    {
      /* Scramble the memory, so that the developer detects invalid references to free'd areas */
      MEM_REGION_SCRAMBLE (copyarea->mem, copyarea->length);
      locator_Keep.copy_areas.areas[locator_Keep.copy_areas.number++] = copyarea;

      pthread_mutex_unlock (&locator_Keep.copy_areas.lock);
    }
  else
    {
      pthread_mutex_unlock (&locator_Keep.copy_areas.lock);

      free_and_init (copyarea);
    }
}

/*
 * locator_pack_copy_area_descriptor: Pack object descriptors for a copy area
 *
 * return:  updated pack area pointer
 *
 *   num_objs(in):  Number of objects
 *   copyarea(in):  Copy area where objects are placed
 *   desc(in): Packed descriptor array
 *
 * NOTE: Pack the desc to be sent over the network from the copy area.
 *              The caller is responsible for determining that desc is large
 *              enough to hold the packed data.
 */
char *
locator_pack_copy_area_descriptor (int num_objs, LC_COPYAREA * copyarea, char *desc, int desc_len)
{
  LC_COPYAREA_MANYOBJS *mobjs;	/* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe on object in area */
  char *ptr;
  int i;

  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (copyarea);

  assert (num_objs <= mobjs->num_objs);

  ptr = desc;
  for (i = 0, obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mobjs); i < num_objs;
       i++, obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj))
    {
      ptr = or_pack_int (ptr, obj->operation);
      ptr = or_pack_int (ptr, obj->flag);
      ptr = or_pack_hfid (ptr, &obj->hfid);
      ptr = or_pack_oid (ptr, &obj->class_oid);
      ptr = or_pack_oid (ptr, &obj->oid);
      ptr = or_pack_int (ptr, obj->length);
      ptr = or_pack_int (ptr, obj->offset);

      assert (CAST_BUFLEN (ptr - desc) <= desc_len);
    }
  return ptr;
}

/*
 * locator_unpack_copy_area_descriptor: Unpack object descriptors for a copy area
 *
 * return: updated pack area pointer
 *
 *   num_objs(in): Number of objects
 *   copyarea(in): Copy area where objects are placed
 *   desc(in): Packed descriptor array
 *
 * NOTE: Unpack the desc sent over the network and place them in the
 *              copy area.  The caller is responsible for determining that
 *              copyarea is large enough to hold the unpacked data.
 */
char *
locator_unpack_copy_area_descriptor (int num_objs, LC_COPYAREA * copyarea, char *desc)
{
  LC_COPYAREA_MANYOBJS *mobjs;	/* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe on object in area */
  int ope;
  int i;

  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (copyarea);
  mobjs->num_objs = num_objs;
  for (i = 0, obj = LC_START_ONEOBJ_PTR_IN_COPYAREA (mobjs); i < num_objs;
       i++, obj = LC_NEXT_ONEOBJ_PTR_IN_COPYAREA (obj))
    {
      desc = or_unpack_int (desc, &ope);
      obj->operation = (LC_COPYAREA_OPERATION) ope;
      desc = or_unpack_int (desc, &obj->flag);
      desc = or_unpack_hfid (desc, &obj->hfid);
      desc = or_unpack_oid (desc, &obj->class_oid);
      desc = or_unpack_oid (desc, &obj->oid);
      desc = or_unpack_int (desc, &obj->length);
      desc = or_unpack_int (desc, &obj->offset);
    }
  return desc;
}

/*
 * locator_send_copy_area: find the active areas to be sent over the net
 *
 * return: number of objects in the copy area
 *
 *   copyarea(in):Copy area where objects are placed
 *   contents_ptr(in):Pointer to content of objects
 *                       (Set as a side effect)
 *   contents_length(in):Length of content area
 *                       (Set as a side effect)
 *   desc_ptr(in):Pointer to descriptor pointer array
 *                       (Set as a side effect)
 *   desc_length(in):Length of descriptor pointer array
 *                       (Set as a side effect)
 *
 * NOTE: Find the active areas (content and descriptor) to be sent over
 *              the network.
 *              The content is sent as is, but the desc are packed.
 *              The caller needs to free *desc_ptr.
 */
int
locator_send_copy_area (LC_COPYAREA * copyarea, char **contents_ptr, int *contents_length, char **desc_ptr,
			int *desc_length)
{
  LC_COPYAREA_MANYOBJS *mobjs;	/* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe on object in area */
  int offset = -1;
  int i, len;
  char *end;

  *contents_ptr = copyarea->mem;

  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (copyarea);
  *desc_length = DB_ALIGN (LC_AREA_ONEOBJ_PACKED_SIZE, MAX_ALIGNMENT) * mobjs->num_objs;
  *desc_ptr = (char *) malloc (*desc_length);

  if (*desc_ptr == NULL)
    {
      *desc_length = 0;
      return 0;
    }

  /* Find the length of the content area and pack the descriptor area */

  if (contents_length != NULL)
    {
      *contents_length = 0;
      if (mobjs->num_objs > 0)
	{
	  obj = &mobjs->objs;
	  obj++;
	  for (i = 0; i < mobjs->num_objs; i++)
	    {
	      obj--;
	      if (obj->offset > offset)
		{
		  /* To the right */
		  *contents_length = obj->length;
		  offset = obj->offset;
		}
	    }

	  if (offset != -1)
	    {
	      int len = *contents_length;
	      int aligned_len = DB_ALIGN (len, MAX_ALIGNMENT);

	      *contents_length = aligned_len + offset;	// total len

#if !defined (NDEBUG)
	      int padded_len = aligned_len - len;
	      if (padded_len > 0)
		{
		  // make valgrind silent
		  memset (*contents_ptr + *contents_length - padded_len, 0, padded_len);
		}
#endif /* DEBUG */
	    }
	}
    }

  end = locator_pack_copy_area_descriptor (mobjs->num_objs, copyarea, *desc_ptr, *desc_length);

  len = CAST_BUFLEN (end - *desc_ptr);
  assert (len <= *desc_length);
  *desc_length = len;

  return mobjs->num_objs;
}

/*
 * locator_recv_allocate_copyarea: allocate a copy area for reciving a "copy area"
 *                         from the net.
 *
 * return: copyarea or NULL(in case of error)
 *
 *   num_objs(in):Number of objects
 *   packed_desc(in):Pointer to packed descriptor array
 *                            (Set as a side effect)
 *   packed_desc_length(in):Length of packec descriptor array
 *   contents_ptr(in):Pointer to content of objects
 *                            (Set as a side effect)
 *   contents_length(in):Length of content area
 *
 * NOTE: Prepare a copy area for receiving a "copyarea" of objects
 *              send by either the client or server.
 */
#if defined(SERVER_MODE)
LC_COPYAREA *
locator_recv_allocate_copyarea (int num_objs, char **contents_ptr, int contents_length)
#else /* SERVER_MODE */
LC_COPYAREA *
locator_recv_allocate_copyarea (int num_objs, char **packed_desc, int packed_desc_length, char **contents_ptr,
				int contents_length)
#endif				/* SERVER_MODE */
{
  LC_COPYAREA *copyarea;
  int length;
  int desc_length;

  if (num_objs > 0)
    {
      num_objs--;
    }

  desc_length = (sizeof (LC_COPYAREA_MANYOBJS) + sizeof (LC_COPYAREA_ONEOBJ) * (num_objs));

  length = contents_length + desc_length + sizeof (LC_COPYAREA);

  copyarea = locator_allocate_copy_area_by_length (length);
  if (copyarea == NULL)
    {
      *contents_ptr = NULL;
    }
  else
    {
      *contents_ptr = copyarea->mem;

#if !defined(SERVER_MODE)
      *packed_desc = (char *) malloc (packed_desc_length);
      if (*packed_desc == NULL)
	{
	  locator_free_copy_area (copyarea);
	  copyarea = NULL;
	  *contents_ptr = NULL;
	}
#endif /* !SERVER_MODE */
    }

  return copyarea;
}

#if defined(CUBRID_DEBUG)
/*
 * locator_dump_string:
 *
 * return:
 *
 *   out_fp(in):output file
 *   dump_string(in):
 *   length(in):
 *
 * NOTE:
 */
static void
locator_dump_string (FILE * out_fp, char *dump_string, int length)
{
  int i;
  for (i = 0; i < length; i++)
    {
      (void) fputc (dump_string[i], out_fp);
    }
}

/*
 * locator_dump_copy_area_one_object:
 *
 * return:
 *
 *   out_fp(in):output file
 *   obj(in):
 *   copyarea(in):Copy area where objects are placed
 *   print_rec(in):true, if records are printed (in ascii format)
 *
 * NOTE:
 */
static void
locator_dump_copy_area_one_object (FILE * out_fp, LC_COPYAREA_ONEOBJ * obj, int obj_index, const LC_COPYAREA * copyarea,
				   int print_rec)
{
  const char *str_operation;
  char *rec;

  switch (obj->operation)
    {
    case LC_FLUSH_INSERT:
    case LC_FLUSH_INSERT_PRUNE:
      str_operation = "FLUSH_INSERT";
      break;
    case LC_FLUSH_DELETE:
      str_operation = "FLUSH_DELETE";
      break;
    case LC_FLUSH_UPDATE:
    case LC_FLUSH_UPDATE_PRUNE:
      str_operation = "FLUSH_UPDATE";
      break;
    case LC_FETCH:
      str_operation = "FETCH";
      break;
    case LC_FETCH_DELETED:
      str_operation = "FETCH_DELETED";
      break;
    case LC_FETCH_DECACHE_LOCK:
      str_operation = "FETCH_DECACHE_LOCK";
      break;
    case LC_FETCH_VERIFY_CHN:
      str_operation = "FETCH_VERIFY_CHN";
      break;
    default:
      str_operation = "UNKNOWN";
      break;
    }
  fprintf (out_fp, "Operation = %s, ", str_operation);
  fprintf (out_fp, "Object OID (volid = %d, pageid = %d, slotid = %d)\n", obj->oid.volid, obj->oid.pageid,
	   obj->oid.slotid);
  fprintf (out_fp, "          length = %d, offset = %d,\n", obj->length, obj->offset);

  fprintf (out_fp, "          Heap (volid = %d, fileid = %d, Hdr_pageid = %d)\n", obj->hfid.vfid.volid,
	   obj->hfid.vfid.fileid, obj->hfid.hpgid);

  if (obj->length < 0
      && (obj->length != -1
	  || (obj->operation != LC_FLUSH_DELETE && obj->operation != LC_FETCH_DELETED
	      && obj->operation != LC_FETCH_DECACHE_LOCK && obj->operation != LC_FETCH_VERIFY_CHN)))
    {
      fprintf (out_fp, "Bad length = %d for object num = %d, OID = %d|%d|%d\n", obj->length, obj_index, obj->oid.volid,
	       obj->oid.pageid, obj->oid.slotid);
    }
  else if (obj->offset > copyarea->length
	   || (obj->offset < 0
	       && (obj->offset != -1
		   || (obj->operation != LC_FLUSH_DELETE && obj->operation != LC_FETCH_DELETED
		       && obj->operation != LC_FETCH_DECACHE_LOCK))))
    {
      fprintf (out_fp, "Bad offset = %d for object num = %d, OID = %d|%d|%d\n", obj->offset, obj_index, obj->oid.volid,
	       obj->oid.pageid, obj->oid.slotid);
    }
  else if (print_rec)
    {
      rec = (char *) copyarea->mem + obj->offset;
      locator_dump_string (out_fp, rec, obj->length);
      fprintf (out_fp, "\n");
    }
}

/*
 * locator_dump_copy_area: dump objects placed in copy area
 *
 * return:
 *
 *   out_fp(in):output file
 *   copyarea(in):Copy area where objects are placed
 *   print_rec(in):true, if records are printed (in ascii format)
 *
 * NOTE: Dump the objects placed in area. The function also detects
 *              some inconsistencies with the copy area. The actual data of
 *              the objects is not dumped.
 *              This function is used for DEBUGGING PURPOSES.
 */
void
locator_dump_copy_area (FILE * out_fp, const LC_COPYAREA * copyarea, int print_rec)
{
  LC_COPYAREA_MANYOBJS *mobjs;	/* Describe multiple objects in area */
  LC_COPYAREA_ONEOBJ *obj;	/* Describe on object in area */
  int i;

  mobjs = LC_MANYOBJS_PTR_IN_COPYAREA (copyarea);
  if (mobjs->num_objs != 0)
    {
      fprintf (out_fp, "\n\n***Dumping fetch/flush area for Num_objs = %d*** \n", mobjs->num_objs);
      obj = &mobjs->objs;
      obj++;
      for (i = 0; i < mobjs->num_objs; i++)
	{
	  obj--;
	  locator_dump_copy_area_one_object (out_fp, obj, i, copyarea, print_rec);
	}
      fprintf (out_fp, "\n\n");
    }
}
#endif

/*
 *
 *       			LOCK FETCH AREAS
 *
 */

/*
 * locator_allocate_lockset: allocate a lockset area for requesting objects
 *
 * return:
 *
 *   max_reqobjs(in):Maximum number of requested objects needed
 *   reqobj_inst_lock(in):The instance lock for the requested objects
 *   reqobj_class_lock(in):The class lock for the requested classes
 *   quit_on_errors(in):Flag which indicate wheter to continue in case of
 *                     errors
 *
 * NOTE: Allocate a flush/fetch area of the given size.
 */
LC_LOCKSET *
locator_allocate_lockset (int max_reqobjs, LOCK reqobj_inst_lock, LOCK reqobj_class_lock, int quit_on_errors)
{
  LC_LOCKSET *lockset = NULL;	/* Area for requested objects */
  int length;
  int i;
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  length = (sizeof (*lockset) + (max_reqobjs * (sizeof (*(lockset->classes)) + sizeof (*(lockset->objects)))));

  /*
   * Do we have an area cached, as big as the one needed ?
   */

  rv = pthread_mutex_lock (&locator_Keep.lockset_areas.lock);

  for (i = 0; i < locator_Keep.lockset_areas.number; i++)
    {
      if (locator_Keep.lockset_areas.areas[i]->length >= length)
	{
	  lockset = locator_Keep.lockset_areas.areas[i];
	  locator_Keep.lockset_areas.areas[i] = locator_Keep.lockset_areas.areas[--locator_Keep.lockset_areas.number];
	  length = lockset->length;
	  max_reqobjs =
	    ((lockset->length - sizeof (*lockset)) / (sizeof (*(lockset->classes)) + sizeof (*(lockset->objects))));

	  /*
	   * Make sure that the caller is not assuming that the area is
	   * initialized to zeros. That is, make sure caller initialize the area
	   */
	  MEM_REGION_SCRAMBLE (lockset, length);

	  break;
	}
    }
  pthread_mutex_unlock (&locator_Keep.lockset_areas.lock);

  if (lockset == NULL)
    {
      lockset = (LC_LOCKSET *) malloc (length);
    }
  if (lockset == NULL)
    {
      return NULL;
    }

  if (locator_initialize_lockset (lockset, length, max_reqobjs, reqobj_inst_lock, reqobj_class_lock, quit_on_errors) !=
      NO_ERROR)
    {
      return NULL;
    }

  return lockset;
}

/*
 * locator_initialize_lockset:
 *
 * return: error code
 *
 *   lockset(in):
 *   length(in):
 *   max_reqobjs(in):Maximum number of requested objects needed
 *   reqobj_inst_lock(in):The instance lock for the requested objects
 *   reqobj_class_lock(in):The class lock for the requested classes
 *   quit_on_errors(in):Flag which indicate wheter to continue in case of
 *                     errors
 *
 * NOTE:
 */
static int
locator_initialize_lockset (LC_LOCKSET * lockset, int length, int max_reqobjs, LOCK reqobj_inst_lock,
			    LOCK reqobj_class_lock, int quit_on_errors)
{
  if (lockset == NULL || length < SSIZEOF (*lockset))
    {
      return ER_FAILED;
    }

  lockset->mem = (char *) lockset;
  lockset->length = length;
  lockset->first_fetch_lockset_call = true;
  lockset->max_reqobjs = max_reqobjs;
  lockset->num_reqobjs = 0;
  lockset->num_reqobjs_processed = -1;
  lockset->last_reqobj_cached = -1;
  lockset->reqobj_inst_lock = reqobj_inst_lock;
  lockset->reqobj_class_lock = reqobj_class_lock;
  lockset->num_classes_of_reqobjs = 0;
  lockset->num_classes_of_reqobjs_processed = -1;
  lockset->last_classof_reqobjs_cached = -1;
  lockset->quit_on_errors = quit_on_errors;
  lockset->packed = NULL;
  lockset->packed_size = 0;
  lockset->classes = ((LC_LOCKSET_CLASSOF *) (lockset->mem + sizeof (*lockset)));
  lockset->objects = ((LC_LOCKSET_REQOBJ *) (lockset->classes + max_reqobjs));

  return NO_ERROR;
}

#if defined (ENABLE_UNUSED_FUNCTION)
/*
 * locator_allocate_lockset_by_length: allocate a lockset area for requesting objects
 *                             (the area is not initialized)
 *
 * return:
 *
 *   length(in):Length of needed area
 *
 * NOTE: Allocate a lockset area by its length. The area is not fully
 *              initialized. It must be initialized by the caller.
 */
LC_LOCKSET *
locator_allocate_lockset_by_length (int length)
{
  LC_LOCKSET *lockset;
  int max_reqobjs;

  max_reqobjs = ((length - sizeof (*lockset)) / (sizeof (*(lockset->classes)) + sizeof (*(lockset->objects))));

  return locator_allocate_lockset (max_reqobjs, NULL_LOCK, NULL_LOCK, true);
}
#endif

/*
 * locator_reallocate_lockset: reallocate a lockset area for requesting objects
 *
 * return: new lockset or NULL
 *
 *   lockset(in):The old area.. This should not be NULL.
 *   max_reqobjs(in):The new size
 *
 * NOTE: Allocate a flush/fetch area of the given size.
 */
LC_LOCKSET *
locator_reallocate_lockset (LC_LOCKSET * lockset, int max_reqobjs)
{
  LC_LOCKSET_REQOBJ *old_reqobjs;
  int oldmax_reqobjs;
  int length;

  length = (sizeof (*lockset) + (max_reqobjs * (sizeof (*(lockset->classes)) + sizeof (*(lockset->objects)))));

  if (lockset->length < length)
    {
      lockset = (LC_LOCKSET *) realloc (lockset, length);
      if (lockset == NULL)
	{
	  return NULL;
	}
    }

  /*
   * Reset to new areas
   */

  oldmax_reqobjs =
    ((lockset->length - (sizeof (*lockset))) / (sizeof (*(lockset->classes)) + sizeof (*(lockset->objects))));

  lockset->mem = (char *) lockset;
  lockset->length = length;
  lockset->max_reqobjs = max_reqobjs;
  lockset->classes = ((LC_LOCKSET_CLASSOF *) (lockset->mem + sizeof (*lockset)));
  lockset->objects = ((LC_LOCKSET_REQOBJ *) (lockset->classes + max_reqobjs));

  /*
   * Need to move the object to the right by the number of positions added
   */
  old_reqobjs = ((LC_LOCKSET_REQOBJ *) (lockset->classes + oldmax_reqobjs));
  memmove (lockset->objects, old_reqobjs, lockset->num_reqobjs * sizeof (*(lockset->objects)));

  return lockset;
}

/*
 * locator_free_lockset : free a lockset area
 *
 * return: nothing
 *
 *   lockset(in):Request area to free
 *
 * NOTE: Free a lockset area
 */
void
locator_free_lockset (LC_LOCKSET * lockset)
{
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  if (lockset->packed)
    {
      locator_free_packed (lockset->packed, lockset->packed_size);
      lockset->packed = NULL;
      lockset->packed_size = 0;
    }

  rv = pthread_mutex_lock (&locator_Keep.lockset_areas.lock);

  if (locator_Keep.lockset_areas.number < LOCATOR_NKEEP_LIMIT)
    {
      /*
       * Scramble the memory, so that the developer detects invalid references
       * to free'd areas
       */
      MEM_REGION_SCRAMBLE ((char *) lockset + sizeof (*lockset), lockset->length - sizeof (*lockset));

      locator_Keep.lockset_areas.areas[locator_Keep.lockset_areas.number++] = lockset;
    }
  else
    {
      free_and_init (lockset);
    }

  pthread_mutex_unlock (&locator_Keep.lockset_areas.lock);
}

#if defined(CUBRID_DEBUG)
/*
 * locator_dump_lockset : Dump objects in lockset area
 *
 * return: nothing
 *
 *   out_fp(in):output file
 *   lockset(in): The lockset area
 *
 * NOTE:
 */
static void
locator_dump_lockset_area_info (FILE * out_fp, LC_LOCKSET * lockset)
{
  fprintf (out_fp, "Mem = %p, length = %d, num_reqobjs = %d,", (void *) (lockset->mem), lockset->length,
	   lockset->num_reqobjs);
  fprintf (out_fp, "Reqobj_inst_lock = %s, Reqobj_class_lock = %s,\n",
	   LOCK_TO_LOCKMODE_STRING (lockset->reqobj_inst_lock), LOCK_TO_LOCKMODE_STRING (lockset->reqobj_class_lock));

  fprintf (out_fp, " num_reqobjs_processed = %d, last_reqobj_cached = %d, \n", lockset->num_reqobjs_processed,
	   lockset->last_reqobj_cached);

  fprintf (out_fp, "num_classes_of_reqobjs = %d, ", lockset->num_classes_of_reqobjs);
  fprintf (out_fp, "num_classes_of_reqobjs_processed = %d, ", lockset->num_classes_of_reqobjs_processed);
  fprintf (out_fp, "last_classof_reqobj_cached = %d", lockset->last_classof_reqobjs_cached);

  fprintf (out_fp, "quit_on_errors = %s, classes = %p, objects = %p\n", (lockset->quit_on_errors ? "TRUE" : "FALSE"),
	   (void *) (lockset->classes), (void *) (lockset->objects));
}

/*
 * locator_dump_lockset_classes :
 *
 * return: nothing
 *
 *   out_fp(in):output file
 *   lockset(in): The lockset area
 *
 * NOTE:
 */
static void
locator_dump_lockset_classes (FILE * out_fp, LC_LOCKSET * lockset)
{
  int i;

  for (i = 0; i < lockset->num_classes_of_reqobjs; i++)
    {
      fprintf (out_fp, "class_oid  = %d|%d|%d, chn = %d\n", lockset->classes[i].oid.volid,
	       lockset->classes[i].oid.pageid, lockset->classes[i].oid.slotid, lockset->classes[i].chn);
    }
}

/*
 * locator_dump_lockset_objects :
 *
 * return: nothing
 *
 *   out_fp(in):output file
 *   lockset(in): The lockset area
 *
 * NOTE:
 */
static void
locator_dump_lockset_objects (FILE * out_fp, LC_LOCKSET * lockset)
{
  int i;

  for (i = 0; i < lockset->num_reqobjs; i++)
    {
      fprintf (out_fp, "object_oid = %d|%d|%d, chn = %d, class_index = %d\n", lockset->objects[i].oid.volid,
	       lockset->objects[i].oid.pageid, lockset->objects[i].oid.slotid, lockset->objects[i].chn,
	       lockset->objects[i].class_index);
    }
}

/*
 * locator_dump_lockset : Dump objects in lockset area
 *
 * return: nothing
 *
 *   out_fp(in):output file
 *   lockset(in): The lockset area
 *
 * NOTE: Dump the lockset area.
 */
void
locator_dump_lockset (FILE * out_fp, LC_LOCKSET * lockset)
{
  int i;

  i = (sizeof (*lockset) + (lockset->num_reqobjs * (sizeof (*lockset->classes) + sizeof (*lockset->objects))));

  if (lockset->length < i || lockset->classes != ((LC_LOCKSET_CLASSOF *) (lockset->mem + sizeof (*lockset)))
      || lockset->objects < ((LC_LOCKSET_REQOBJ *) (lockset->classes + lockset->num_reqobjs)))
    {
      fprintf (out_fp, "Area is inconsistent: either area is too small %d", lockset->length);
      fprintf (out_fp, " (expect at least %d),\n", i);
      fprintf (out_fp, " pointer to classes %p (expected %p), or\n", (void *) (lockset->classes),
	       (void *) (lockset->mem + sizeof (*lockset)));
      fprintf (out_fp, " pointer to objects %p (expected >= %p) are incorrect\n", (void *) (lockset->objects),
	       (void *) (lockset->classes + lockset->num_reqobjs));
      return;
    }

  fprintf (out_fp, "\n***Dumping lockset area***\n");
  locator_dump_lockset_area_info (out_fp, lockset);

  locator_dump_lockset_classes (out_fp, lockset);
  locator_dump_lockset_objects (out_fp, lockset);

}
#endif

/*
 * locator_allocate_and_unpack_lockset: allocate a lockset area and unpack the given
 *                          area onto it
 *
 * return: lockset area
 *
 *   unpacked(in):Area to unpack
 *   unpacked_size(in):Size of unpacked area
 *   unpack_classes(in):whether to unpack classes
 *   unpack_objects(in):whether to unpack objects
 *   reg_unpacked(in):Whether the unpacked area is register as part of
 *                       lockset area
 *
 * NOTE: Allocate a lockset. Then unpack the given area onto it.
 */
LC_LOCKSET *
locator_allocate_and_unpack_lockset (char *unpacked, int unpacked_size, bool unpack_classes, bool unpack_objects,
				     bool reg_unpacked)
{
  char *ptr;
  LC_LOCKSET *lockset;
  int max_reqobjs;

  ptr = unpacked;
  ptr = or_unpack_int (ptr, &max_reqobjs);	/* Really first call */
  ptr = or_unpack_int (ptr, &max_reqobjs);

  lockset = locator_allocate_lockset (max_reqobjs, NULL_LOCK, NULL_LOCK, true);
  if (lockset == NULL)
    {
      return NULL;
    }

  lockset->packed = unpacked;
  lockset->packed_size = unpacked_size;

  (void) locator_unpack_lockset (lockset, unpack_classes, unpack_objects);

  if (reg_unpacked == false)
    {
      lockset->packed = NULL;
      lockset->packed_size = 0;
    }

  return lockset;
}

/*
 * locator_pack_lockset_header : Pack the lockset area header
 *
 * return:
 *
 *   packed(in):
 *   lockset(in/out):Lockfetch area to pack
 *
 * NOTE: that we do not pack several fileds such as: last_reqobj_cached,
 *       las_classof_reqobjs_cached.
 */
static char *
locator_pack_lockset_header (char *packed, LC_LOCKSET * lockset)
{
  packed = or_pack_int (packed, lockset->first_fetch_lockset_call ? 1 : 0);
  packed = or_pack_int (packed, lockset->max_reqobjs);
  packed = or_pack_int (packed, lockset->num_reqobjs);
  packed = or_pack_int (packed, lockset->num_reqobjs_processed);
  packed = or_pack_int (packed, (int) lockset->reqobj_inst_lock);
  packed = or_pack_int (packed, (int) lockset->reqobj_class_lock);
  packed = or_pack_int (packed, lockset->num_classes_of_reqobjs);
  packed = or_pack_int (packed, lockset->num_classes_of_reqobjs_processed);
  packed = or_pack_int (packed, lockset->quit_on_errors);

  return packed;
}

/*
 * locator_pack_lockset_classes : Pack the lockset area classes
 *
 * return:
 *
 *   packed(in):
 *   lockset(in/out):Lockfetch area to pack
 *
 * NOTE:
 */
static char *
locator_pack_lockset_classes (char *packed, LC_LOCKSET * lockset)
{
  LC_LOCKSET_CLASSOF *class_lockset;
  int i;

  for (i = 0, class_lockset = lockset->classes; i < lockset->num_classes_of_reqobjs; i++, class_lockset++)
    {
      packed = or_pack_oid (packed, &class_lockset->oid);
      packed = or_pack_int (packed, class_lockset->chn);
    }

  return packed;
}

/*
 * locator_pack_lockset_objects : Pack the lockset area objects
 *
 * return:
 *
 *   packed(in):
 *   lockset(in/out):Lockfetch area to pack
 *
 * NOTE:
 */
static char *
locator_pack_lockset_objects (char *packed, LC_LOCKSET * lockset)
{
  LC_LOCKSET_REQOBJ *object;
  int i;

  for (i = 0, object = lockset->objects; i < lockset->num_reqobjs; i++, object++)
    {
      packed = or_pack_oid (packed, &object->oid);
      packed = or_pack_int (packed, object->chn);
      packed = or_pack_int (packed, object->class_index);
    }

  return packed;
}

/*
 * locator_pack_lockset : Pack the lockset area
 *
 * return: number of bytes that were packed
 *      lockset packed fileds are set as a side effect,
 *
 *   lockset(in/out):Lockfetch area to pack
 *   pack_classes(in):whether to pack classes
 *   pack_objects(in):whether to pack objects
 *
 * NOTE: Allocate an area to pack the lockset area to be sent over the
 *              network. The address and size of the packed area is left on
 *              the lockset area as a side effect. If there was an area
 *              already present, it is used.
 *              Then, the lockset area is packed onto this area, the amount of
 *              packing is returned.
 */
int
locator_pack_lockset (LC_LOCKSET * lockset, bool pack_classes, bool pack_objects)
{
  char *packed;
  int packed_size;

  packed_size = LC_LOCKSET_PACKED_SIZE (lockset);

  /*
   * Do we have space for packing ?
   */

  if (lockset->packed != NULL)
    {
      /*
       * Reuse the current area
       */
      if (packed_size > lockset->packed_size)
	{
	  /*
	   * We need to realloc this area
	   */
	  packed = locator_reallocate_packed (lockset->packed, packed_size);
	  if (packed == NULL)
	    {
	      return 0;
	    }

	  lockset->packed = packed;
	  lockset->packed_size = packed_size;
	}
      packed = lockset->packed;
    }
  else
    {
      packed = locator_allocate_packed (packed_size);
      if (packed == NULL)
	{
	  return 0;
	}
      lockset->packed = packed;
      lockset->packed_size = packed_size;
    }

  packed = locator_pack_lockset_header (packed, lockset);

  /*
   * Pack the classes of requested objects
   */

  if (pack_classes)
    {
      packed = locator_pack_lockset_classes (packed, lockset);
    }

  /*
   * Pack the requested objects
   */

  if (pack_objects)
    {
      packed = locator_pack_lockset_objects (packed, lockset);
    }

  return CAST_BUFLEN (packed - lockset->packed);
}

/*
 * locator_pack_lockset_header : Pack the lockset area header
 *
 * return:
 *
 *   packed(in):
 *   lockset(in/out):Lockfetch area to pack
 *
 * NOTE: that we do not pack several fileds such as: last_reqobj_cached,
 *      las_classof_reqobjs_cached.
 */
static char *
locator_unpack_lockset_header (char *unpacked, LC_LOCKSET * lockset)
{
  int first_fetch;

  unpacked = or_unpack_int (unpacked, &first_fetch);
  lockset->first_fetch_lockset_call = (first_fetch == 1) ? true : false;
  unpacked = or_unpack_int (unpacked, &lockset->max_reqobjs);
  unpacked = or_unpack_int (unpacked, &lockset->num_reqobjs);
  unpacked = or_unpack_int (unpacked, &lockset->num_reqobjs_processed);
  unpacked = or_unpack_int (unpacked, (int *) &lockset->reqobj_inst_lock);
  unpacked = or_unpack_int (unpacked, (int *) &lockset->reqobj_class_lock);
  unpacked = or_unpack_int (unpacked, &lockset->num_classes_of_reqobjs);
  unpacked = or_unpack_int (unpacked, &lockset->num_classes_of_reqobjs_processed);
  unpacked = or_unpack_int (unpacked, &lockset->quit_on_errors);

  return unpacked;
}

/*
 * locator_pack_lockset_classes : Pack the lockset area classes
 *
 * return:
 *
 *   packed(in):
 *   lockset(in/out):Lockfetch area to pack
 *
 * NOTE:
 */
static char *
locator_unpack_lockset_classes (char *unpacked, LC_LOCKSET * lockset)
{
  LC_LOCKSET_CLASSOF *class_lockset;
  int i;

  for (i = 0, class_lockset = lockset->classes; i < lockset->num_classes_of_reqobjs; i++, class_lockset++)
    {
      unpacked = or_unpack_oid (unpacked, &class_lockset->oid);
      unpacked = or_unpack_int (unpacked, &class_lockset->chn);
    }

  return unpacked;
}

/*
 * locator_pack_lockset_objects : Pack the lockset area objects
 *
 * return:
 *
 *   packed(in):
 *   lockset(in/out):Lockfetch area to pack
 *
 * NOTE:
 */
static char *
locator_unpack_lockset_objects (char *unpacked, LC_LOCKSET * lockset)
{
  LC_LOCKSET_REQOBJ *object;
  int i;

  for (i = 0, object = lockset->objects; i < lockset->num_reqobjs; i++, object++)
    {
      unpacked = or_unpack_oid (unpacked, &object->oid);
      unpacked = or_unpack_int (unpacked, &object->chn);
      unpacked = or_unpack_int (unpacked, &object->class_index);
    }

  return unpacked;
}

/*
 * locator_unpack_lockset : unpack a lockset area
 *
 * return: number of bytes that were unpacked
 *
 *   lockset(in/out):Request area (set as a side effect)
 *   unpack_classes(in):whether to unpack classes
 *   unpack_objects(in):whether to unpack objects
 *
 * NOTE: Unpack the lockset area which was sent over the network.
 */
int
locator_unpack_lockset (LC_LOCKSET * lockset, bool unpack_classes, bool unpack_objects)
{
  char *unpacked;

  unpacked = lockset->packed;
  unpacked = locator_unpack_lockset_header (unpacked, lockset);

  /*
   * Unpack the classes of requested objects
   */

  if (unpack_classes)
    {
      unpacked = locator_unpack_lockset_classes (unpacked, lockset);
    }

  /*
   * Unpack the requested objects
   */

  if (unpack_objects)
    {
      unpacked = locator_unpack_lockset_objects (unpacked, lockset);
    }

  return CAST_BUFLEN (unpacked - lockset->packed);
}

/*
 * locator_allocate_lockhint : allocate a lockhint area for prelocking and prefetching
 *                    classes during parsing
 *
 * return: LC_LOCKHINT * or NULL
 *
 *   max_classes(in):Maximum number of classes
 *   quit_on_errors(in):Flag which indicate wheter to continue in case of
 *                     errors
 *
 * NOTE: Allocate a lockhint areas.
 */
LC_LOCKHINT *
locator_allocate_lockhint (int max_classes, bool quit_on_errors)
{
  LC_LOCKHINT *lockhint = NULL;
  int length;
  int i;
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  length = sizeof (*lockhint) + (max_classes * sizeof (*(lockhint->classes)));

  /* Do we have a lockhint area cached ? */

  rv = pthread_mutex_lock (&locator_Keep.lockhint_areas.lock);

  for (i = 0; i < locator_Keep.lockhint_areas.number; i++)
    {
      if (locator_Keep.lockhint_areas.areas[i]->length >= length)
	{
	  lockhint = locator_Keep.lockhint_areas.areas[i];
	  locator_Keep.lockhint_areas.areas[i] =
	    locator_Keep.lockhint_areas.areas[--locator_Keep.lockhint_areas.number];
	  length = lockhint->length;
	  max_classes = ((lockhint->length - sizeof (*lockhint)) / sizeof (*(lockhint->classes)));

	  /*
	   * Make sure that the caller is not assuming that the area is
	   * initialized to zeros. That is, make sure caller initialize the area
	   */
	  MEM_REGION_SCRAMBLE (lockhint, length);

	  break;
	}
    }

  pthread_mutex_unlock (&locator_Keep.lockhint_areas.lock);

  if (lockhint == NULL)
    {
      lockhint = (LC_LOCKHINT *) malloc (length);
    }
  if (lockhint == NULL)
    {
      return NULL;
    }

  if (locator_initialize_lockhint (lockhint, length, max_classes, quit_on_errors) != NO_ERROR)
    {
      return NULL;
    }

  return lockhint;
}

/*
 * locator_initialize_lockhint:
 *
 * return: error code
 *
 *   lockhint(in):
 *   length(in):
 *   max_classes(in):Maximum number of classes
 *   quit_on_errors(in):Flag which indicate wheter to continue in case of
 *                     errors
 *
 * NOTE:
 */
static int
locator_initialize_lockhint (LC_LOCKHINT * lockhint, int length, int max_classes, bool quit_on_errors)
{
  if (lockhint == NULL || length < SSIZEOF (*lockhint))
    {
      return ER_FAILED;
    }

  lockhint->mem = (char *) lockhint;
  lockhint->length = length;
  lockhint->first_fetch_lockhint_call = true;
  lockhint->max_classes = max_classes;
  lockhint->num_classes = 0;
  lockhint->num_classes_processed = -1;
  lockhint->quit_on_errors = quit_on_errors;
  lockhint->packed = NULL;
  lockhint->packed_size = 0;
  lockhint->classes = ((struct lc_lockhint_class *) (lockhint->mem + sizeof (*lockhint)));

  return NO_ERROR;
}

/*
 * locator_reallocate_lockhint: reallocate a lockhint area for prelocking and
 *                     prefetching classes during parsing
 *
 * return: LC_LOCKHINT * or NULL
 *
 *   lockhint(in):The old lockhint area.. This should not be NULL
 *   max_classes(in):The maximum number of classes
 *
 * NOTE: Reallocate a lockhint areas for lockhinting and prefetching
 *              purposes during parsing.
 */
LC_LOCKHINT *
locator_reallocate_lockhint (LC_LOCKHINT * lockhint, int max_classes)
{
  int length;

  length = sizeof (*lockhint) + (max_classes * sizeof (*(lockhint->classes)));

  if (lockhint->length < length)
    {
      lockhint = (LC_LOCKHINT *) realloc (lockhint, length);
      if (lockhint == NULL)
	{
	  return NULL;
	}

      /* Reset to new areas */
      lockhint->mem = (char *) lockhint;
      lockhint->length = length;
      lockhint->max_classes = max_classes;
      lockhint->classes = ((struct lc_lockhint_class *) (lockhint->mem + sizeof (*lockhint)));
    }

  return lockhint;
}

/*
 * locator_free_lockhint : free a lockhint area
 *
 * return: nothing
 *
 *   lockhint(in):Hintlock area to free
 *
 * NOTE: Free a lockhint area
 */
void
locator_free_lockhint (LC_LOCKHINT * lockhint)
{
#if defined (SERVER_MODE)
  int rv;
#endif /* SERVER_MODE */

  if (lockhint->packed)
    {
      locator_free_packed (lockhint->packed, lockhint->packed_size);
      lockhint->packed = NULL;
      lockhint->packed_size = 0;
    }

  rv = pthread_mutex_lock (&locator_Keep.lockhint_areas.lock);

  if (locator_Keep.lockhint_areas.number < LOCATOR_NKEEP_LIMIT)
    {
      /*
       * Scramble the memory, so that the developer detects invalid references
       * to free'd areas
       */
      MEM_REGION_SCRAMBLE ((char *) lockhint + sizeof (*lockhint), lockhint->length - sizeof (*lockhint));
      locator_Keep.lockhint_areas.areas[locator_Keep.lockhint_areas.number++] = lockhint;
    }
  else
    {
      free_and_init (lockhint);
    }

  pthread_mutex_unlock (&locator_Keep.lockhint_areas.lock);
}

#if defined(CUBRID_DEBUG)
/*
 * locator_dump_lockhint_info :
 *
 * return: nothing
 *
 *   out_fp(in): output file
 *   lockhint(in):Hintlock area to dump
 *
 * NOTE:
 */
static void
locator_dump_lockhint_info (FILE * out_fp, LC_LOCKHINT * lockhint)
{
  fprintf (out_fp, "Mem = %p, len = %d, max_classes = %d, num_classes = %d\n", (void *) (lockhint->mem),
	   lockhint->length, lockhint->max_classes, lockhint->num_classes);

  fprintf (out_fp, " num_classes_processed = %d,\n", lockhint->num_classes_processed);
}

/*
 * locator_dump_lockhint_classes :
 *
 * return: nothing
 *
 *   out_fp(in): output file
 *   lockhint(in):Hintlock area to dump
 *
 * NOTE:
 */
static void
locator_dump_lockhint_classes (FILE * out_fp, LC_LOCKHINT * lockhint)
{
  int i;

  for (i = 0; i < lockhint->num_classes; i++)
    {
      fprintf (out_fp, "class_oid  = %d|%d|%d, chn = %d, lock = %s, subclasses = %d\n", lockhint->classes[i].oid.volid,
	       lockhint->classes[i].oid.pageid, lockhint->classes[i].oid.slotid, lockhint->classes[i].chn,
	       LOCK_TO_LOCKMODE_STRING (lockhint->classes[i].lock), lockhint->classes[i].need_subclasses);
    }
}

/*
 * locator_dump_lockhint : dump a lockhint area
 *
 * return: nothing
 *
 *   out_fp(in): output file
 *   lockhint(in):Hintlock area to dump
 *
 * NOTE: Dump the information placed in lockhint area.
 *              This function is used for DEBUGGING PURPOSES.
 */
void
locator_dump_lockhint (FILE * out_fp, LC_LOCKHINT * lockhint)
{
  fprintf (out_fp, "\n***Dumping lockhint area***\n");
  locator_dump_lockhint_info (out_fp, lockhint);

  locator_dump_lockhint_classes (out_fp, lockhint);
}
#endif

/*
 * locator_allocate_and_unpack_lockhint : allocate a lockhint area and unpack the given
 *                           area onto it
 *
 * return: LC_LOCKHINT * or NULL
 *
 *   unpacked(in):Area to unpack
 *   unpacked_size(in):Size of unpacked area
 *   unpack_classes(in):whether to unpack classes
 *   reg_unpacked(in):Whether the unpacked area is register as part of
 *                       lockhint area
 *
 * NOTE: Allocate a lockhint area. Then unpack the given area onto it.
 */
LC_LOCKHINT *
locator_allocate_and_unpack_lockhint (char *unpacked, int unpacked_size, bool unpack_classes, bool reg_unpacked)
{
  int max_classes;
  char *ptr;
  LC_LOCKHINT *lockhint = NULL;

  ptr = unpacked;
  ptr = or_unpack_int (ptr, &max_classes);

  lockhint = locator_allocate_lockhint (max_classes, true);
  if (lockhint == NULL)
    {
      return NULL;
    }

  lockhint->packed = unpacked;
  lockhint->packed_size = unpacked_size;

  (void) locator_unpack_lockhint (lockhint, unpack_classes);

  if (reg_unpacked == false)
    {
      lockhint->packed = NULL;
      lockhint->packed_size = 0;
    }

  return lockhint;
}

/*
 * locator_pack_lockhint_header :
 *
 * return:
 *
 *   packed(in):
 *   lockhint(in/out):Hintlock area to pack
 *
 * NOTE:
 */
static char *
locator_pack_lockhint_header (char *packed, LC_LOCKHINT * lockhint)
{
  packed = or_pack_int (packed, lockhint->max_classes);
  packed = or_pack_int (packed, lockhint->num_classes);
  packed = or_pack_int (packed, lockhint->num_classes_processed);
  packed = or_pack_int (packed, lockhint->quit_on_errors);

  return packed;
}

/*
 * locator_pack_lockhint_classes :
 *
 * return:
 *
 *   packed(in):
 *   lockhint(in/out):Hintlock area to pack
 *
 * NOTE:
 */
static char *
locator_pack_lockhint_classes (char *packed, LC_LOCKHINT * lockhint)
{
  LC_LOCKHINT_CLASS *class_lockhint;
  int i;

  for (i = 0, class_lockhint = lockhint->classes; i < lockhint->num_classes; i++, class_lockhint++)
    {
      packed = or_pack_oid (packed, &class_lockhint->oid);
      packed = or_pack_int (packed, class_lockhint->chn);
      packed = or_pack_lock (packed, class_lockhint->lock);
      packed = or_pack_int (packed, class_lockhint->need_subclasses);
    }

  return packed;
}

/*
 * locator_pack_lockhint : pack to lockhint area
 *
 * return: number of bytes that were packed
 *      lockhint packed fileds are set as a side effect.
 *
 *   lockhint(in/out):Hintlock area to pack
 *   pack_classes(in):whether to pack classes
 *
 * NOTE: Allocate an area to pack the lockhint area to be sent over
 *              the network. The address and size of the packed area is left
 *              on the lockhint area as a side effect. If there was an area
 *              already present, it is used.
 *              Then, the lockhint area is packed onto this area, the amount
 *              of packing is returned.
 */
int
locator_pack_lockhint (LC_LOCKHINT * lockhint, bool pack_classes)
{
  char *packed;
  int packed_size;

  packed_size = LC_LOCKHINT_PACKED_SIZE (lockhint);

  /*
   * Do we have space for packing ?
   */

  if (lockhint->packed != NULL)
    {
      /*
       * Reuse the current area
       */
      if (packed_size > lockhint->packed_size)
	{
	  /*
	   * We need to realloc this area
	   */
	  packed = locator_reallocate_packed (lockhint->packed, packed_size);
	  if (packed == NULL)
	    {
	      return 0;
	    }

	  lockhint->packed = packed;
	  lockhint->packed_size = packed_size;
	}
      packed = lockhint->packed;
    }
  else
    {
      packed = locator_allocate_packed (packed_size);
      if (packed == NULL)
	{
	  return 0;
	}

      lockhint->packed = packed;
      lockhint->packed_size = packed_size;
    }

  packed = locator_pack_lockhint_header (packed, lockhint);

  if (pack_classes)
    {
      packed = locator_pack_lockhint_classes (packed, lockhint);
    }

  return CAST_BUFLEN (packed - lockhint->packed);
}

/*
 * locator_unpack_lockhint_header :
 *
 * return:
 *
 *   unpacked(in):
 *   lockhint(in/out):Hintlock area to pack
 *
 * NOTE:
 */
static char *
locator_unpack_lockhint_header (char *unpacked, LC_LOCKHINT * lockhint)
{
  unpacked = or_unpack_int (unpacked, &lockhint->max_classes);
  unpacked = or_unpack_int (unpacked, &lockhint->num_classes);
  unpacked = or_unpack_int (unpacked, &lockhint->num_classes_processed);
  unpacked = or_unpack_int (unpacked, &lockhint->quit_on_errors);

  return unpacked;
}

/*
 * locator_unpack_lockhint_classes :
 *
 * return:
 *
 *   packed(in):
 *   lockhint(in/out):Hintlock area to pack
 *
 * NOTE:
 */
static char *
locator_unpack_lockhint_classes (char *unpacked, LC_LOCKHINT * lockhint)
{
  LC_LOCKHINT_CLASS *class_lockhint;
  int i;

  for (i = 0, class_lockhint = lockhint->classes; i < lockhint->num_classes; i++, class_lockhint++)
    {
      unpacked = or_unpack_oid (unpacked, &class_lockhint->oid);
      unpacked = or_unpack_int (unpacked, &class_lockhint->chn);
      unpacked = or_unpack_lock (unpacked, &class_lockhint->lock);
      unpacked = or_unpack_int (unpacked, &class_lockhint->need_subclasses);
    }

  return unpacked;
}

/*
 * locator_unpack_lockhint : unpack a lockhint area
 *
 * return: number of bytes that were unpacked
 *
 *   lockhint(in/out):Hintlock area to unpack (set as a side effect)
 *   unpack_classes(in):whether to unpack classes
 *
 * NOTE: Unpack the lockhint area which was sent over the network.
 */
int
locator_unpack_lockhint (LC_LOCKHINT * lockhint, bool unpack_classes)
{
  char *unpacked;

  unpacked = lockhint->packed;

  unpacked = locator_unpack_lockhint_header (unpacked, lockhint);

  if (unpack_classes)
    {
      unpacked = locator_unpack_lockhint_classes (unpacked, lockhint);
    }

  return CAST_BUFLEN (unpacked - lockhint->packed);
}

/*
 *
 *       		       LC_OIDSET PACKING
 *
 */

/*
 * locator_make_oid_set () -
 *
 * return: new oidset structure
 *
 * NOTE:
 *    This creates a root LC_OIDSET structure, intended for incremental
 *    population using locator_add_oid_set.
 *    Free it with locator_free_oid_set when you're done.
 */
LC_OIDSET *
locator_make_oid_set (void)
{
  LC_OIDSET *set;

  set = (LC_OIDSET *) db_private_alloc (NULL, sizeof (LC_OIDSET));
  if (set == NULL)
    {
      return NULL;
    }

  set->total_oids = 0;
  set->num_classes = 0;
  set->classes = NULL;
  set->is_list = true;

  return set;
}

/*
 * locator_clear_oid_set () -
 *
 * return: nothing
 *
 *   oidset(in):oidset to clear
 *
 * NOTE:
 *    Frees the entries inside an LC_OIDSET but leaves the outer structure
 *    in place.  This could be used in places where we build up a partial
 *    oidset, flush it, and then fill it up again.
 *    This is a little complicated as there are two different styles for
 *    allocation.  During incremental LC_OIDSET construction, we'll use
 *    a linked list but the server will unpack it using arrays.  This
 *    style is maintained in an internal is_list flag in the appropriate
 *    places.
 *    This saves having to mess with growing arrays.
 */
void
locator_clear_oid_set (THREAD_ENTRY * thread_p, LC_OIDSET * oidset)
{
  LC_CLASS_OIDSET *class_oidset, *c_next;
  LC_OIDMAP *oid, *o_next;

  if (oidset != NULL)
    {
      /* map over the classes */
      if (oidset->classes != NULL)
	{
#if defined (SERVER_MODE)
	  if (thread_p == NULL)
	    {
	      thread_p = thread_get_thread_entry_info ();
	    }
#endif // SERVER_MODE

	  for (class_oidset = oidset->classes, c_next = NULL; class_oidset != NULL; class_oidset = c_next)
	    {
	      c_next = class_oidset->next;

	      /* on the client, its important that we NULL out these pointers in case they happen to point to MOPs, we
	       * don't want to leave GC roots lying around. */
	      for (oid = class_oidset->oids; oid != NULL; oid = oid->next)
		{
		  oid->mop = NULL;
		  oid->client_data = NULL;
		}

	      /* free either the list or array of OID map elements */
	      if (!class_oidset->is_list)
		{
		  db_private_free_and_init (thread_p, class_oidset->oids);
		}
	      else
		{
		  for (oid = class_oidset->oids, o_next = NULL; oid != NULL; oid = o_next)
		    {
		      o_next = oid->next;
		      db_private_free_and_init (thread_p, oid);
		    }
		}
	      /* if we have a list of classes, free them as we go */
	      if (oidset->is_list)
		{
		  db_private_free_and_init (thread_p, class_oidset);
		}
	    }

	  /* if we have an array of classes, free it at the end */
	  if (!oidset->is_list)
	    {
	      db_private_free_and_init (thread_p, oidset->classes);
	    }
	}

      oidset->total_oids = 0;
      oidset->num_classes = 0;
      oidset->classes = NULL;
      oidset->is_list = true;
    }
}

/*
 * locator_free_oid_set () -
 *
 * return: nothing
 *
 *   oidset(in):oidset to free
 *
 * NOTE:
 *    Frees memory associated with an LC_OIDSET.
 */
void
locator_free_oid_set (THREAD_ENTRY * thread_p, LC_OIDSET * oidset)
{
  if (oidset != NULL)
    {
      locator_clear_oid_set (thread_p, oidset);
      db_private_free_and_init (thread_p, oidset);
    }
}

/*
 * locator_add_oid_set () -
 *
 * return: LC_OIDMAP* or NULL
 *
 *   set(in):oidset to extend
 *   heap(in):class heap id
 *   class_oid(in):class OID
 *   obj_oid(in):the currently temporary object OID
 *
 * NOTE:
 *    Adds another temporary OID entry to an LC_OIDSET and returns
 *    the internal LC_OIDMAP structure associated with that OID.
 *    NULL is returned on error.
 *
 *    This is normally called by the client with a temporary OID, we check
 *    to make sure that the same temporary OID is not added twice.
 */
LC_OIDMAP *
locator_add_oid_set (THREAD_ENTRY * thread_p, LC_OIDSET * set, HFID * heap, OID * class_oid, OID * obj_oid)
{
  LC_CLASS_OIDSET *class_oidset_p;
  LC_OIDMAP *oidmap_p;

  oidmap_p = NULL;

  /* sanity test, can't extend into fixed structures */
  if (set == NULL || !set->is_list)
    {
      /* can't have a temporary OID without a cached class */
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return NULL;
    }

  /* see if we already have an entry for this class */
  for (class_oidset_p = set->classes; class_oidset_p != NULL; class_oidset_p = class_oidset_p->next)
    {
      /* MOP comparison would be faster but makes the structre more complex */
      if (locator_is_hfid_equal (heap, &(class_oidset_p->hfid)))
	{
	  break;
	}
    }

#if defined (SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
#endif // SERVER_MODE

  if (class_oidset_p == NULL)
    {
      /* haven't seen this class yet, add a new entry */
      class_oidset_p = (LC_CLASS_OIDSET *) db_private_alloc (thread_p, sizeof (LC_CLASS_OIDSET));
      if (class_oidset_p == NULL)
	{
	  return NULL;
	}
      oidmap_p = (LC_OIDMAP *) db_private_alloc (thread_p, sizeof (LC_OIDMAP));
      if (oidmap_p == NULL)
	{
	  db_private_free_and_init (thread_p, class_oidset_p);
	  return NULL;
	}

      oidmap_p->next = NULL;
      oidmap_p->oid = *obj_oid;
      oidmap_p->est_size = 0;
      oidmap_p->mop = NULL;
      oidmap_p->client_data = NULL;

      class_oidset_p->class_oid = *class_oid;
      class_oidset_p->hfid = *heap;
      class_oidset_p->is_list = true;
      class_oidset_p->oids = oidmap_p;
      class_oidset_p->num_oids = 1;
      class_oidset_p->next = set->classes;
      set->classes = class_oidset_p;
      set->num_classes++;
      set->total_oids++;
    }
  else
    {
      /* already have a list for this class, add another object if we don't have one already */
      for (oidmap_p = class_oidset_p->oids; oidmap_p != NULL; oidmap_p = oidmap_p->next)
	{
	  if (OID_EQ (&oidmap_p->oid, obj_oid))
	    {
	      break;
	    }
	}
      if (oidmap_p == NULL)
	{
	  /* never seen this particular temp oid, add another entry */
	  oidmap_p = (LC_OIDMAP *) db_private_alloc (thread_p, sizeof (LC_OIDMAP));
	  if (oidmap_p == NULL)
	    {
	      return NULL;
	    }
	  oidmap_p->next = class_oidset_p->oids;
	  class_oidset_p->oids = oidmap_p;
	  oidmap_p->oid = *obj_oid;
	  oidmap_p->est_size = 0;
	  oidmap_p->mop = NULL;
	  oidmap_p->client_data = NULL;
	  class_oidset_p->num_oids++;
	  set->total_oids++;
	}
    }

  return oidmap_p;
}

/*
 * locator_get_packed_oid_set_size () -
 *
 * return: packed size
 *
 *   oidset(in):oidset to ponder
 *
 * Note:
 *    Returns the number of bytes it would take to create the packed
 *    representation of an LC_OIDSET as would be produced by
 *    locator_pack_oid_set.
 */
int
locator_get_packed_oid_set_size (LC_OIDSET * oidset)
{
  LC_CLASS_OIDSET *class_oidset;
  int size, count;

  size = OR_INT_SIZE;		/* number of classes */
  for (class_oidset = oidset->classes, count = 0; class_oidset != NULL; class_oidset = class_oidset->next, count++)
    {
      size += OR_OID_SIZE;
      size += OR_HFID_SIZE;
      size += OR_INT_SIZE;
      size += class_oidset->num_oids * (OR_OID_SIZE + OR_INT_SIZE);
    }

  /* sanity check on count, should set an error or something */
  if (count != oidset->num_classes)
    {
      oidset->num_classes = count;
    }

  return size;
}

/*
 * locator_pack_oid_set () -
 *
 * return: advanced pointer
 *
 *   buffer(in):buffer in which to pack
 *   oidset(in):oidset to pack
 *
 * NOTE:
 *    Packs the flattened representation of an oidset into a buffer.
 *    The buffer must be of an appropriate size, you should always call
 *    locator_get_packed_oid_set_size first.
 */
char *
locator_pack_oid_set (char *buffer, LC_OIDSET * oidset)
{
  LC_CLASS_OIDSET *class_oidset;
  LC_OIDMAP *oid;

  buffer = or_pack_int (buffer, oidset->num_classes);
  for (class_oidset = oidset->classes; class_oidset != NULL; class_oidset = class_oidset->next)
    {
      buffer = or_pack_oid (buffer, &class_oidset->class_oid);
      buffer = or_pack_hfid (buffer, &class_oidset->hfid);
      buffer = or_pack_int (buffer, class_oidset->num_oids);

      for (oid = class_oidset->oids; oid != NULL; oid = oid->next)
	{
	  buffer = or_pack_oid (buffer, &oid->oid);
	  buffer = or_pack_int (buffer, oid->est_size);
	}
    }
  return buffer;
}

/*
 * locator_unpack_oid_set () -
 *
 * return: unpacked oidset
 *
 *   buffer(in):buffer containing packed representation
 *   use(in):existing oidset structure to unpack into
 *
 * NOTE:
 *    This unpacks the packed representation of an oidset and either updates
 *    an existing structure or creates and returns a new one.
 *    If an existing structure is supplied, it MUST be of exactly the same
 *    format as the one used to create the packed representation.
 *    This is intended for use on the client after it has sent an oidset
 *    over to the server for the permanent OID's to be assigned.  The
 *    thing we get back will be identical with only changes to the packed
 *    OIDs so we don't have to waste time allocating a new structure, we
 *    can just unpack into the existing structure.
 *
 *    If we're on the server side, we won't have a "use" structure so we
 *    just allocate a new one.  Note that the server uses arrays for
 *    the class & oidmap lists rather than linked lists.
 */
bool
locator_unpack_oid_set_to_exist (char *buffer, LC_OIDSET * use)
{
  LC_CLASS_OIDSET *class_oidset;
  LC_OIDMAP *oid;
  LC_OIDSET *set = NULL;
  int c, o;
  char *ptr;

  ptr = buffer;

  if (use == NULL)
    {
      return false;
    }

  set = use;
  /* unpack into an existing structure, it better be large enough */
  ptr = or_unpack_int (ptr, &c);
  if (c != set->num_classes)
    {
      goto use_error;
    }

  for (class_oidset = set->classes; class_oidset != NULL; class_oidset = class_oidset->next)
    {
      /* skip these, could check for consistency */
      ptr += OR_OID_SIZE;
      ptr += OR_HFID_SIZE;
      ptr = or_unpack_int (ptr, &o);
      if (o != class_oidset->num_oids)
	{
	  goto use_error;
	}

      for (oid = class_oidset->oids; oid != NULL; oid = oid->next)
	{
	  /* this is what we came for */
	  ptr = or_unpack_oid (ptr, &oid->oid);
	  /* can skip this */
	  ptr += OR_INT_SIZE;
	  /* note that we must leave mop & client_data fields untouched !! */
	}
    }

  /* success */
  return true;

use_error:
  /* need something appropriate */
  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
  return false;
}

LC_OIDSET *
locator_unpack_oid_set_to_new (THREAD_ENTRY * thread_p, char *buffer)
{
  LC_CLASS_OIDSET *class_oidset;
  LC_OIDMAP *oid;
  LC_OIDSET *set;
  int c, o, total;
  char *ptr;

  ptr = buffer;

#if defined (SERVER_MODE)
  if (thread_p == NULL)
    {
      thread_p = thread_get_thread_entry_info ();
    }
#endif // SERVER_MODE

  /* we have to unpack and build a new structure, use arrays */
  set = (LC_OIDSET *) db_private_alloc (thread_p, sizeof (LC_OIDSET));
  if (set == NULL)
    {
      goto memory_error;
    }

  total = 0;
  ptr = or_unpack_int (ptr, &set->num_classes);
  if (!set->num_classes)
    {
      set->classes = NULL;
    }
  else
    {
      set->classes = (LC_CLASS_OIDSET *) db_private_alloc (thread_p, sizeof (LC_CLASS_OIDSET) * set->num_classes);
      if (set->classes == NULL)
	{
	  goto memory_error;
	}
      set->is_list = false;

      /* initialize things so we can cleanup easier */
      for (c = 0, class_oidset = set->classes; c < set->num_classes; c++, class_oidset++)
	{
	  if (c == set->num_classes - 1)
	    {
	      class_oidset->next = NULL;
	    }
	  else
	    {
	      class_oidset->next = class_oidset + 1;
	    }
	  class_oidset->oids = NULL;
	}

      /* load the class data */
      for (c = 0, class_oidset = set->classes; c < set->num_classes; c++, class_oidset++)
	{
	  ptr = or_unpack_oid (ptr, &class_oidset->class_oid);
	  ptr = or_unpack_hfid (ptr, &class_oidset->hfid);
	  ptr = or_unpack_int (ptr, &class_oidset->num_oids);

	  class_oidset->oids = (LC_OIDMAP *) db_private_alloc (thread_p, sizeof (LC_OIDMAP) * class_oidset->num_oids);
	  if (class_oidset->oids == NULL)
	    {
	      goto memory_error;
	    }
	  class_oidset->is_list = false;

	  /* load the oid data */
	  for (o = 0, oid = class_oidset->oids; o < class_oidset->num_oids; o++, oid++)
	    {
	      if (o == class_oidset->num_oids - 1)
		{
		  oid->next = NULL;
		}
	      else
		{
		  oid->next = oid + 1;
		}
	      ptr = or_unpack_oid (ptr, &oid->oid);
	      ptr = or_unpack_int (ptr, &oid->est_size);
	      oid->mop = NULL;
	      oid->client_data = NULL;
	      total++;
	    }
	}
      set->total_oids = total;
    }

  /* success */
  return set;

memory_error:
  locator_free_oid_set (thread_p, set);

  return NULL;
}

bool
locator_manyobj_flag_is_set (LC_COPYAREA_MANYOBJS * copyarea, enum MULTI_UPDATE_FLAG muf)
{
  return copyarea->multi_update_flags & muf;
}

void
locator_manyobj_flag_remove (LC_COPYAREA_MANYOBJS * copyarea, enum MULTI_UPDATE_FLAG muf)
{
  assert (locator_manyobj_flag_is_set (copyarea, muf));
  copyarea->multi_update_flags &= (~muf);
}

void
locator_manyobj_flag_set (LC_COPYAREA_MANYOBJS * copyarea, enum MULTI_UPDATE_FLAG muf)
{
  copyarea->multi_update_flags |= muf;
}
