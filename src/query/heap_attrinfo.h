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
 * Heap attribute info - interface between heap and query modules
 */

#ifndef _HEAP_ATTRINFO_H_
#define _HEAP_ATTRINFO_H_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "object_representation_sr.h"

typedef enum
{
  HEAP_READ_ATTRVALUE,
  HEAP_WRITTEN_ATTRVALUE,
  HEAP_UNINIT_ATTRVALUE,
  HEAP_WRITTEN_LOB_ATTRVALUE
} HEAP_ATTRVALUE_STATE;

typedef enum
{
  HEAP_INSTANCE_ATTR,
  HEAP_SHARED_ATTR,
  HEAP_CLASS_ATTR
} HEAP_ATTR_TYPE;

typedef struct heap_attrvalue HEAP_ATTRVALUE;
struct heap_attrvalue
{
  ATTR_ID attrid;		/* attribute identifier */
  HEAP_ATTRVALUE_STATE state;	/* State of the attribute value. Either of has been read, has been updated, or is
				 * not initialized */
  int do_increment;
  HEAP_ATTR_TYPE attr_type;	/* Instance, class, or shared attribute */
  OR_ATTRIBUTE *last_attrepr;	/* Used for default values */
  OR_ATTRIBUTE *read_attrepr;	/* Pointer to a desired attribute information */
  DB_VALUE dbvalue;		/* DB values of the attribute in memory */
};

typedef struct heap_cache_attrinfo HEAP_CACHE_ATTRINFO;
struct heap_cache_attrinfo
{
  OID class_oid;		/* Class object identifier */
  int last_cacheindex;		/* An index identifier when the last_classrepr was obtained from the classrepr cache.
				 * Otherwise, -1 */
  int read_cacheindex;		/* An index identifier when the read_classrepr was obtained from the classrepr cache.
				 * Otherwise, -1 */
  OR_CLASSREP *last_classrepr;	/* Currently cached catalog attribute info. */
  OR_CLASSREP *read_classrepr;	/* Currently cached catalog attribute info. */
  OID inst_oid;			/* Instance Object identifier */
  int inst_chn;			/* Current chn of instance object */
  int num_values;		/* Number of desired attribute values */
  HEAP_ATTRVALUE *values;	/* Value for the attributes */
};

#endif /* _HEAP_ATTRINFO_H_ */
