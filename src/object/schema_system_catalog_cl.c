/*
 *
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

#include "schema_system_catalog_cl.h"

#include "authenticate.h"
#include "dbi.h"
#include "dbtype.h"
#include "error_manager.h"
#include "locator_cl.h"
#include "object_accessor.h"
#include "schema_manager.h"
#include "string_opfunc.h"

static int set_catalog_timestamps (MOP catalog_inst, SM_CATALOG_TIMESTAMP_TYPE type, DB_VALUE *datetime_val);
static MOP find_class_catalog_instance_with_write_mode (MOP _db_class, const char *class_name);

static int
set_catalog_timestamps (MOP catalog_inst, SM_CATALOG_TIMESTAMP_TYPE type, DB_VALUE *datetime_val)
{
  int error = NO_ERROR;
  DB_VALUE local_datetime_val;

  if (datetime_val == NULL)
    {
      error = db_sys_datetime (&local_datetime_val);
      if (error != NO_ERROR)
	{
	  return error;
	}
      datetime_val = &local_datetime_val;
    }

  switch (type)
    {
    case SM_CATALOG_TIMESTAMP_INIT:
      error = obj_set (catalog_inst, "created_time", datetime_val);
      if (error != NO_ERROR)
	{
	  return error;
	}
      [[fallthrough]];
    case SM_CATALOG_TIMESTAMP_UPDATE:
      error = obj_set (catalog_inst, "updated_time", datetime_val);
      if (error != NO_ERROR)
	{
	  return error;
	}
      break;
    case SM_CATALOG_TIMESTAMP_STATISTICS:
      error = obj_set (catalog_inst, "checked_time", datetime_val);
      if (error != NO_ERROR)
	{
	  return error;
	}
      break;
    default:
      assert (false);
      break;
    }

  return error;
}

static MOP
find_class_catalog_instance_with_write_mode (MOP _db_class, const char *class_name)
{
  MOP catalog_inst = NULL;
  DB_VALUE class_name_val;

  db_make_string (&class_name_val, class_name);
  catalog_inst = db_find_unique_write_mode (_db_class, "unique_name", &class_name_val);
  if (catalog_inst == NULL)
    {
      ASSERT_ERROR();
      goto end;
    }

end:
  db_value_clear (&class_name_val);
  return catalog_inst;
}

int
sm_set_class_catalog_timestamps (const char *class_name, SM_CATALOG_TIMESTAMP_TYPE type)
{
  int error = NO_ERROR;
  MOP catalog_inst;
  int save;
  MOP _db_class = NULL;

  AU_DISABLE (save);

  _db_class = db_find_class (CT_CLASS_NAME);
  if (_db_class == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  catalog_inst = find_class_catalog_instance_with_write_mode(_db_class, class_name);
  if (catalog_inst == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  error = set_catalog_timestamps (catalog_inst, type, NULL);

end:
  AU_ENABLE (save);
  return error;
}

int
sm_set_class_catalog_timestamps_all_classes (void)
{
  LIST_MOPS *lmops = NULL;
  int error = NO_ERROR;
  MOP _db_class = NULL;
  MOP inst = NULL;
  DB_VALUE datetime_val;
  int save;
  const char *class_name = NULL;

  AU_DISABLE (save);

  _db_class = db_find_class (CT_CLASS_NAME);
  if (_db_class == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  lmops = locator_get_all_mops (sm_Root_class_mop, DB_FETCH_READ, NULL);
  if (lmops == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  error = db_sys_datetime (&datetime_val);
  if (error != NO_ERROR)
    {
      goto end;
    }

  for (int i = 0; i < lmops->num; i++)
    {
      class_name = sm_get_ch_name (lmops->mops[i]);
      inst = find_class_catalog_instance_with_write_mode(_db_class, class_name);
      if (inst == NULL)
	{
	  ASSERT_ERROR_AND_SET (error);
	  goto end;
	}

      error = set_catalog_timestamps ( inst, SM_CATALOG_TIMESTAMP_INIT, &datetime_val);
      if (error != NO_ERROR)
	{
	  goto end;
	}
    }

end:
  AU_ENABLE (save);
  return error;
}

int
sm_set_class_catalog_statistics_info (MOP _db_class, const char *class_name, CLASS_STATS *stats, bool with_fullscan)
{
  int save;
  MOP catalog_inst;
  int error = NO_ERROR;
  DB_VALUE timestamp_val, statistics_strategy_val, datetime_val;

  AU_DISABLE (save);

  catalog_inst = find_class_catalog_instance_with_write_mode(_db_class, class_name);
  if (catalog_inst == NULL)
    {
      ASSERT_ERROR_AND_SET (error);
      goto end;
    }

  db_make_timestamp (&timestamp_val, stats->time_stamp);
  db_timestamp_to_datetime (&timestamp_val, &datetime_val);
  error = set_catalog_timestamps (catalog_inst, SM_CATALOG_TIMESTAMP_STATISTICS, &datetime_val);
  if (error != NO_ERROR)
    {
      goto end;
    }

  db_make_int (&statistics_strategy_val, with_fullscan);
  error = obj_set (catalog_inst, "statistics_strategy", &statistics_strategy_val);
  if (error != NO_ERROR)
    {
      goto end;
    }

end:
  AU_ENABLE (save);
  return error;
}