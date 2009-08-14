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
 * cm_nameval.h -
 */

#ifndef _CM_NAMEVAL_H_
#define _CM_NAMEVAL_H_

#ident "$Id$"

#include <time.h>
#include "cm_config.h"
#include "cm_dstring.h"

#define NV_ADD_DATE		0
#define NV_ADD_TIME		1
#define NV_ADD_DATE_TIME	2

typedef struct nvp_t
{
  dstring *name;
  dstring *value;
} nvpair;

typedef struct nvplist_t
{
  nvpair **nvpairs;
  int nvplist_size;		/* allocated list length */
  int nvplist_leng;		/* number of valid nvpairs in nvp_list */

  dstring *listopener;
  dstring *listcloser;

  dstring *delimiter;		/* ":"  */
  dstring *endmarker;		/* "\n" */
} nvplist;

/* user interface functions */
nvplist *nv_create (int defsize, const char *lom, const char *lcm,
		    const char *dm, const char *em);
int nv_init (nvplist * ref, int defsize, const char *lom, const char *lcm,
	     const char *dm, const char *em);
int nv_lookup (nvplist * ref, int index, char **name, char **value);
int nv_locate (nvplist * ref, const char *marker, int *index, int *ilen);
char *nv_get_val (nvplist * ref, const char *name);
int nv_update_val (nvplist * ref, const char *name, const char *value);
int nv_update_val_int (nvplist * ref, const char *name, int value);
void nv_destroy (nvplist * ref);
int nv_writeto (nvplist * ref, char *filename);
void nv_reset_nvp (nvplist * ref);
int nv_add_nvp (nvplist * ref, const char *name, const char *value);
int nv_add_nvp_int64 (nvplist * ref, const char *name, INT64 value);
int nv_add_nvp_int (nvplist * ref, const char *name, int value);
int nv_add_nvp_time (nvplist * ref, const char *name, time_t t,
		     const char *fmt, int flat);
int nv_add_nvp_float (nvplist * ref, const char *name, float value,
		      const char *fmt);
int nv_readfrom (nvplist * ref, char *filename);

#endif /* _CM_NAMEVAL_H_ */
