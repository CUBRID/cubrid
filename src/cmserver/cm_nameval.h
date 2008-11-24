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
 * cm_nameval.h - 
 */

#ifndef _CM_NAMEVAL_H_
#define _CM_NAMEVAL_H_

#ident "$Id$"

#include <time.h>

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
nvplist *nv_create (int defsize, char *lom, char *lcm, char *dm, char *em);
int nv_init (nvplist * ref, int defsize, char *lom, char *lcm, char *dm,
	     char *em);
int nv_lookup (nvplist * ref, int index, char **name, char **value);
int nv_locate (nvplist * ref, char *marker, int *index, int *ilen);
char *nv_get_val (nvplist * ref, char *name);
int nv_update_val (nvplist * ref, const char *name, const char *value);
int nv_update_val_int (nvplist * ref, const char *name, int value);
void nv_destroy (nvplist * ref);
int nv_writeto (nvplist * ref, char *filename);
void nv_reset_nvp (nvplist * ref);
int nv_add_nvp (nvplist * ref, const char *name, const char *value);
int nv_add_nvp_int (nvplist * ref, const char *name, int value);
int nv_add_nvp_time (nvplist * ref, const char *name, time_t t, char *fmt,
		     int flat);
int nv_add_nvp_float (nvplist * ref, const char *name, float value,
		      char *fmt);
int nv_readfrom (nvplist * ref, char *filename);

#endif /* _CM_NAMEVAL_H_ */
