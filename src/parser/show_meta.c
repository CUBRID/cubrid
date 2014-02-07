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
 * show_meta.c -  show statement infos, including column defination, semantic check.
 */

#ident "$Id$"


#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>

#include "show_meta.h"
#include "error_manager.h"
#include "parser.h"

static bool show_Inited = false;
static SHOWSTMT_METADATA *show_Metas[SHOWSTMT_END];

static int init_db_attribute_list (SHOWSTMT_METADATA * md);
static void free_db_attribute_list (SHOWSTMT_METADATA * md);

/* check functions */

/* meta functions */
static SHOWSTMT_METADATA *metadata_of_volume_header (void);

static SHOWSTMT_METADATA *
metadata_of_volume_header (void)
{
  static const SHOWSTMT_COLUMN cols[] = {
    {"Volume_id", "int"},
    {"Magic_symbol", "varchar(100)"},
    {"Io_page_size", "short"},
    {"Purpose", "varchar(24)"},
    {"Sector_size_in_pages", "int"},
    {"Num_total_sectors", "int"},
    {"Num_free_sectors", "int"},
    {"Hint_alloc_sector", "int"},
    {"Num_total_pages", "int"},
    {"Num_free_pages", "int"},
    {"Sector_alloc_table_size_in_pages", "int"},
    {"Sector_alloc_table_first_page", "int"},
    {"Page_alloc_table_size_in_pages", "int"},
    {"Page_alloc_table_first_page", "int"},
    {"Last_system_page", "int"},
    {"Creation_time", "datetime"},
    {"Num_max_pages", "int"},
    {"Num_used_data_pages", "int"},
    {"Num_used_index_pages", "int"},
    {"Checkpoint_lsa", "varchar(64)"},
    {"Boot_hfid", "varchar(64)"},
    {"Full_name", "varchar(64)"},
    {"Next_vol_full_name", "varchar(255)"},
    {"Remarks", "varchar(64)"}
  };
  static const SHOWSTMT_COLUMN_ORDERBY orderby[] = {
    {1, PT_ASC}
  };
  static const SHOWSTMT_NAMED_ARG args[] = {
    {NULL, AVT_INTEGER, false}
  };
  static SHOWSTMT_METADATA md = {
    SHOWSTMT_VOLUME_HEADER, "show volume header of ",
    cols, DIM (cols), orderby, DIM (orderby), args, DIM (args), NULL, NULL
  };

  return &md;
}

/*
 * showstmt_get_metadata() -  return show statment column infos
 *   return:-
 *   show_type(in): SHOW statement type
 */
const SHOWSTMT_METADATA *
showstmt_get_metadata (SHOWSTMT_TYPE show_type)
{
  const SHOWSTMT_METADATA *show_meta = NULL;

  assert_release (SHOWSTMT_START < show_type && show_type < SHOWSTMT_END);

  show_meta = show_Metas[show_type];
  assert_release (show_meta != NULL && show_meta->show_type == show_type);
  return show_meta;
}

/*
 * showstmt_get_attributes () -  return all DB_ATTRIBUTE
 *   return:-
 *   show_type(in): SHOW statement type
 */
DB_ATTRIBUTE *
showstmt_get_attributes (SHOWSTMT_TYPE show_type)
{
  const SHOWSTMT_METADATA *show_meta = NULL;

  show_meta = showstmt_get_metadata (show_type);

  return show_meta->showstmt_attrs;
}


/*
 * init_db_attribute_list () : init DB_ATTRIBUTE list for each show statement
 *   return: error code
 *   md(in/out):
 */
static int
init_db_attribute_list (SHOWSTMT_METADATA * md)
{
  int i;
  DB_DOMAIN *domain;
  DB_ATTRIBUTE *attrs = NULL, *att;

  if (md == NULL)
    {
      return NO_ERROR;
    }

  for (i = md->num_cols - 1; i >= 0; i--)
    {
      domain = pt_string_to_db_domain (md->cols[i].type, NULL);
      if (domain == NULL)
	{
	  goto on_error;
	}
      domain = tp_domain_cache (domain);

      att =
	classobj_make_attribute (md->cols[i].name, domain->type,
				 ID_ATTRIBUTE);
      if (att == NULL)
	{
	  goto on_error;
	}
      att->domain = domain;
      att->auto_increment = NULL;

      if (attrs == NULL)
	{
	  attrs = att;
	}
      else
	{
	  att->order_link = attrs;
	  attrs = att;
	}
    }

  md->showstmt_attrs = attrs;
  return NO_ERROR;

on_error:
  while (attrs != NULL)
    {
      att = attrs;
      attrs = (DB_ATTRIBUTE *) att->order_link;
      att->order_link = NULL;

      classobj_free_attribute (att);
    }

  return er_errid ();
}

/*
 * free_db_attribute_list () : free DB_ATTRIBUTE list for each show statement
 *   return: 
 *   md(in/out):
 */
static void
free_db_attribute_list (SHOWSTMT_METADATA * md)
{
  DB_ATTRIBUTE *attrs = NULL, *att;

  if (md == NULL)
    {
      return;
    }

  attrs = md->showstmt_attrs;
  md->showstmt_attrs = NULL;
  while (attrs != NULL)
    {
      att = attrs;
      attrs = (DB_ATTRIBUTE *) att->order_link;
      att->order_link = NULL;

      classobj_free_attribute (att);
    }
}

/*
 * showstmt_metadata_init() -- initialize the metadata of show statements
 * return error code>
 */
int
showstmt_metadata_init (void)
{
  int error;
  unsigned int i;

  if (show_Inited)
    {
      return NO_ERROR;
    }

  memset (show_Metas, 0, sizeof (show_Metas));
  show_Metas[SHOWSTMT_VOLUME_HEADER] = metadata_of_volume_header ();

  for (i = 0; i < DIM (show_Metas); i++)
    {
      error = init_db_attribute_list (show_Metas[i]);
      if (error != NO_ERROR)
	{
	  goto on_error;
	}
    }
  show_Inited = true;
  return NO_ERROR;

on_error:
  for (i = 0; i < DIM (show_Metas); i++)
    {
      free_db_attribute_list (show_Metas[i]);
    }
  return error;
}

/*
 * showstmt_metadata_final() -- free the metadata of show statements
 */
void
showstmt_metadata_final (void)
{
  unsigned int i;

  if (!show_Inited)
    {
      return;
    }

  for (i = 0; i < DIM (show_Metas); i++)
    {
      free_db_attribute_list (show_Metas[i]);
    }
  show_Inited = false;
}
