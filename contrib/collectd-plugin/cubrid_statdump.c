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

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "perf_monitor.h"
#include "db.h"

/* cubrid broker include files */
#include "cubrid_statdump.h"
#define PLUGIN_NAME	"cubrid_statdump"

static const char *config_keys[] = {
  "database",
  NULL
};

static int config_keys_num = 1;

static char *db = NULL;
static int is_restarted = false;
static int
config (const char *key, const char *value)
{
  if (strcasecmp (key, "database") == 0)
    {
      db = strdup (value);
      return 1;
    }
  return -1;
}

static int cubrid_statdump_init (void);
static int cubrid_statdump_read (void);
static void submit (char *type, char *type_instance, value_t * values,
		    int value_cnt);

static int
cubrid_statdump_init (void)
{
  return 0;
}

static int
cubrid_statdump_shutdown (void)
{
  if (is_restarted == true)
    {
      histo_stop ();
      db_shutdown ();
    }

  return 0;
}

static int
cubrid_statdump_read ()
{
  char submit_name[256];

  /* Execution statistics for the file io */
  value_t file_num_creates[1];
  value_t file_num_removes[1];
  value_t file_num_ioreads[1];
  value_t file_num_iowrites[1];
  value_t file_num_iosynches[1];

  /* Execution statistics for the page buffer manager */
  value_t pb_num_fetches[1];
  value_t pb_num_dirties[1];
  value_t pb_num_ioreads[1];
  value_t pb_num_iowrites[1];
  value_t pb_num_victims[1];
  value_t pb_num_replacements[1];

  /* Execution statistics for the log manager */
  value_t log_num_ioreads[1];
  value_t log_num_iowrites[1];
  value_t log_num_appendrecs[1];
  value_t log_num_archives[1];
  value_t log_num_checkpoints[1];
  value_t log_num_wals[1];

  /* Execution statistics for the lock manager */
  value_t lk_num_acquired_on_pages[1];
  value_t lk_num_acquired_on_objects[1];
  value_t lk_num_converted_on_pages[1];
  value_t lk_num_converted_on_objects[1];
  value_t lk_num_re_requested_on_pages[1];
  value_t lk_num_re_requested_on_objects[1];
  value_t lk_num_waited_on_pages[1];
  value_t lk_num_waited_on_objects[1];

  /* Execution statistics for transactions */
  value_t tran_num_commits[1];
  value_t tran_num_rollbacks[1];
  value_t tran_num_savepoints[1];
  value_t tran_num_start_topops[1];
  value_t tran_num_end_topops[1];
  value_t tran_num_interrupts[1];

  /* Execution statistics for the btree manager */
  value_t bt_num_inserts[1];
  value_t bt_num_deletes[1];
  value_t bt_num_updates[1];
  value_t bt_num_covered[1];
  value_t bt_num_noncovered[1];
  value_t bt_num_resumes[1];

  /* Execution statistics for the query manger */
  value_t qm_num_selects[1];
  value_t qm_num_inserts[1];
  value_t qm_num_deletes[1];
  value_t qm_num_updates[1];
  value_t qm_num_sscans[1];
  value_t qm_num_iscans[1];
  value_t qm_num_lscans[1];
  value_t qm_num_setscans[1];
  value_t qm_num_methscans[1];
  value_t qm_num_nljoins[1];
  value_t qm_num_mjoins[1];
  value_t qm_num_objfetches[1];

  /* flush control */
  value_t fc_num_pages[1];
  value_t fc_num_log_pages[1];
  value_t fc_tokens[1];

  /* Execution statistics for network communication */
  value_t net_num_requests[1];

  /* Execution statistics for Plan cache */
  value_t pc_num_add[1];
  value_t pc_num_lookup[1];
  value_t pc_num_hit[1];
  value_t pc_num_miss[1];
  value_t pc_num_full[1];
  value_t pc_num_delete[1];
  value_t pc_num_invalid_xasl_id[1];
  value_t pc_num_query_string_hash_entries[1];
  value_t pc_num_xasl_id_hash_entries[1];
  value_t pc_num_class_oid_hash_entries[1];

  value_t pb_hit_ratio[1];

  MNT_SERVER_EXEC_STATS mystat;
  
  if (is_restarted == false)
    {
      /* restart needed */
      AU_DISABLE_PASSWORDS ();

      db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
      db_login ("dba", NULL);
      if (db != NULL && (db_restart (db, TRUE, db) == NO_ERROR))
	{
	  histo_start (true);
	  is_restarted = true;
	}
      else
	{
	  return 0;
	}
    }

  memset (&mystat, '\0', sizeof (MNT_SERVER_EXEC_STATS));

  sprintf (submit_name, "statdump_%s", db);

  mnt_get_global_diff_stats (&mystat);

  file_num_creates[0].gauge = mystat.file_num_creates;
  file_num_removes[0].gauge = mystat.file_num_removes;
  file_num_ioreads[0].gauge = mystat.file_num_ioreads;
  file_num_iowrites[0].gauge = mystat.file_num_iowrites;
  file_num_iosynches[0].gauge = mystat.file_num_iosynches;

  pb_num_fetches[0].gauge = mystat.pb_num_fetches;
  pb_num_dirties[0].gauge = mystat.pb_num_dirties;
  pb_num_ioreads[0].gauge = mystat.pb_num_ioreads;
  pb_num_iowrites[0].gauge = mystat.pb_num_iowrites;
  pb_num_victims[0].gauge = mystat.pb_num_victims;
  pb_num_replacements[0].gauge = mystat.pb_num_replacements;

  log_num_ioreads[0].gauge = mystat.log_num_ioreads;
  log_num_iowrites[0].gauge = mystat.log_num_iowrites;
  log_num_appendrecs[0].gauge = mystat.log_num_appendrecs;
  log_num_archives[0].gauge = mystat.log_num_archives;
  log_num_checkpoints[0].gauge = mystat.log_num_checkpoints;
  log_num_wals[0].gauge = mystat.log_num_wals;

  lk_num_acquired_on_pages[0].gauge = mystat.lk_num_acquired_on_pages;
  lk_num_acquired_on_objects[0].gauge = mystat.lk_num_acquired_on_objects;
  lk_num_converted_on_pages[0].gauge = mystat.lk_num_converted_on_pages;
  lk_num_converted_on_objects[0].gauge = mystat.lk_num_converted_on_objects;
  lk_num_re_requested_on_pages[0].gauge = mystat.lk_num_re_requested_on_pages;
  lk_num_re_requested_on_objects[0].gauge =
    mystat.lk_num_re_requested_on_objects;
  lk_num_waited_on_pages[0].gauge = mystat.lk_num_waited_on_pages;
  lk_num_waited_on_objects[0].gauge = mystat.lk_num_waited_on_objects;

  tran_num_commits[0].gauge = mystat.tran_num_commits;
  tran_num_rollbacks[0].gauge = mystat.tran_num_rollbacks;
  tran_num_savepoints[0].gauge = mystat.tran_num_savepoints;
  tran_num_start_topops[0].gauge = mystat.tran_num_start_topops;
  tran_num_end_topops[0].gauge = mystat.tran_num_end_topops;
  tran_num_interrupts[0].gauge = mystat.tran_num_interrupts;

  bt_num_inserts[0].gauge = mystat.bt_num_inserts;
  bt_num_deletes[0].gauge = mystat.bt_num_deletes;
  bt_num_updates[0].gauge = mystat.bt_num_updates;
  bt_num_covered[0].gauge = mystat.bt_num_covered;
  bt_num_noncovered[0].gauge = mystat.bt_num_noncovered;
  bt_num_resumes[0].gauge = mystat.bt_num_resumes;

  qm_num_selects[0].gauge = mystat.qm_num_selects;
  qm_num_inserts[0].gauge = mystat.qm_num_inserts;
  qm_num_deletes[0].gauge = mystat.qm_num_deletes;
  qm_num_updates[0].gauge = mystat.qm_num_updates;
  qm_num_sscans[0].gauge = mystat.qm_num_sscans;
  qm_num_iscans[0].gauge = mystat.qm_num_iscans;
  qm_num_lscans[0].gauge = mystat.qm_num_lscans;
  qm_num_setscans[0].gauge = mystat.qm_num_setscans;
  qm_num_methscans[0].gauge = mystat.qm_num_methscans;
  qm_num_nljoins[0].gauge = mystat.qm_num_nljoins;
  qm_num_mjoins[0].gauge = mystat.qm_num_mjoins;
  qm_num_objfetches[0].gauge = mystat.qm_num_objfetches;

  fc_num_pages[0].gauge = mystat.fc_num_pages;
  fc_num_log_pages[0].gauge = mystat.fc_num_log_pages;
  fc_tokens[0].gauge = mystat.fc_tokens;

  net_num_requests[0].gauge = mystat.net_num_requests;

  pc_num_add[0].gauge = mystat.pc_num_add;
  pc_num_lookup[0].gauge = mystat.pc_num_lookup;
  pc_num_hit[0].gauge = mystat.pc_num_hit;
  pc_num_miss[0].gauge = mystat.pc_num_miss;
  pc_num_full[0].gauge = mystat.pc_num_full;
  pc_num_delete[0].gauge = mystat.pc_num_delete;
  pc_num_invalid_xasl_id[0].gauge = mystat.pc_num_invalid_xasl_id;
  pc_num_query_string_hash_entries[0].gauge =
    mystat.pc_num_query_string_hash_entries;
  pc_num_xasl_id_hash_entries[0].gauge =
    mystat.pc_num_xasl_id_hash_entries;
  pc_num_class_oid_hash_entries[0].gauge =
    mystat.pc_num_class_oid_hash_entries;

  pb_hit_ratio[0].gauge = mystat.pb_hit_ratio;

  submit ("file_num_creates", submit_name, file_num_creates, 1);
  submit ("file_num_removes", submit_name, file_num_removes, 1);
  submit ("file_num_ioreads", submit_name, file_num_ioreads, 1);
  submit ("file_num_iowrites", submit_name, file_num_iowrites, 1);
  submit ("file_num_iosynches", submit_name, file_num_iosynches, 1);

  submit ("pb_num_fetches", submit_name, pb_num_fetches, 1);
  submit ("pb_num_dirties", submit_name, pb_num_dirties, 1);
  submit ("pb_num_ioreads", submit_name, pb_num_ioreads, 1);
  submit ("pb_num_iowrites", submit_name, pb_num_iowrites, 1);
  submit ("pb_num_victims", submit_name, pb_num_victims, 1);
  submit ("pb_num_replacements", submit_name, pb_num_replacements, 1);

  submit ("log_num_ioreads", submit_name, log_num_ioreads, 1);
  submit ("log_num_iowrites", submit_name, log_num_iowrites, 1);
  submit ("log_num_appendrecs", submit_name, log_num_appendrecs, 1);
  submit ("log_num_archives", submit_name, log_num_archives, 1);
  submit ("log_num_checkpoints", submit_name, log_num_checkpoints, 1);
  submit ("log_num_wals", submit_name, log_num_wals, 1);

  submit ("lk_num_acquired_on_pages", submit_name, lk_num_acquired_on_pages,
	  1);
  submit ("lk_num_acquired_on_objects", submit_name,
	  lk_num_acquired_on_objects, 1);
  submit ("lk_num_converted_on_pages", submit_name, lk_num_converted_on_pages,
	  1);
  submit ("lk_num_converted_on_objects", submit_name,
	  lk_num_converted_on_objects, 1);
  submit ("lk_num_re_requested_on_pages", submit_name,
	  lk_num_re_requested_on_pages, 1);
  submit ("lk_num_re_requested_on_objects", submit_name,
	  lk_num_re_requested_on_objects, 1);
  submit ("lk_num_waited_on_pages", submit_name, lk_num_waited_on_pages, 1);
  submit ("lk_num_waited_on_objects", submit_name, lk_num_waited_on_objects,
	  1);

  submit ("tran_num_commits", submit_name, tran_num_commits, 1);
  submit ("tran_num_rollbacks", submit_name, tran_num_rollbacks, 1);
  submit ("tran_num_savepoints", submit_name, tran_num_savepoints, 1);
  submit ("tran_num_start_topops", submit_name, tran_num_start_topops, 1);
  submit ("tran_num_end_topops", submit_name, tran_num_end_topops, 1);
  submit ("tran_num_interrupts", submit_name, tran_num_interrupts, 1);

  submit ("bt_num_inserts", submit_name, bt_num_inserts, 1);
  submit ("bt_num_deletes", submit_name, bt_num_deletes, 1);
  submit ("bt_num_updates", submit_name, bt_num_updates, 1);
  submit ("bt_num_covered", submit_name, bt_num_covered, 1);
  submit ("bt_num_noncovered", submit_name, bt_num_noncovered, 1);
  submit ("bt_num_resumes", submit_name, bt_num_resumes, 1);

  submit ("qm_num_selects", submit_name, qm_num_selects, 1);
  submit ("qm_num_inserts", submit_name, qm_num_inserts, 1);
  submit ("qm_num_deletes", submit_name, qm_num_deletes, 1);
  submit ("qm_num_updates", submit_name, qm_num_updates, 1);
  submit ("qm_num_sscans", submit_name, qm_num_sscans, 1);
  submit ("qm_num_iscans", submit_name, qm_num_iscans, 1);
  submit ("qm_num_lscans", submit_name, qm_num_lscans, 1);
  submit ("qm_num_setscans", submit_name, qm_num_setscans, 1);
  submit ("qm_num_nljoins", submit_name, qm_num_nljoins, 1);
  submit ("qm_num_mjoins", submit_name, qm_num_mjoins, 1);
  submit ("qm_num_objfetches", submit_name, qm_num_objfetches, 1);

  submit ("fc_num_pages", submit_name, fc_num_pages, 1);
  submit ("fc_num_log_pages", submit_name, fc_num_log_pages, 1);
  submit ("fc_tokens", submit_name, fc_tokens, 1);

  submit ("net_num_requests", submit_name, net_num_requests, 1);

  submit ("pc_num_add", submit_name, pc_num_add, 1);
  submit ("pc_num_lookup", submit_name, pc_num_lookup, 1);
  submit ("pc_num_hit", submit_name, pc_num_hit, 1);
  submit ("pc_num_miss", submit_name, pc_num_miss, 1);
  submit ("pc_num_full", submit_name, pc_num_full, 1);
  submit ("pc_num_delete", submit_name, pc_num_delete, 1);
  submit ("pc_num_invalid_xasl_id", submit_name, pc_num_invalid_xasl_id, 1);
  submit ("pc_num_query_string_hash_entries", submit_name,
	  pc_num_query_string_hash_entries, 1);
  submit ("pc_num_xasl_id_hash_entries", submit_name, 
          pc_num_xasl_id_hash_entries, 1);
  submit ("pc_num_class_oid_hash_entries", submit_name,
	  pc_num_class_oid_hash_entries, 1);

  submit ("pb_hit_ratio", submit_name, pb_hit_ratio, 1);

  return 0;
}

static void
submit (char *type, char *type_instance, value_t * values, int value_cnt)
{
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = values;
  vl.values_len = value_cnt;
  vl.time = time (NULL);
  strcpy (vl.host, hostname_g);
  strcpy (vl.plugin, PLUGIN_NAME);
  strcpy (vl.plugin_instance, "");
  strcpy (vl.type_instance, type_instance);
#if defined (COLLECTD_43)
  plugin_dispatch_values (type, &vl);
#else
  strcpy (vl.type, type);
  plugin_dispatch_values (&vl);
#endif

}

void
module_register (void)
{
  printf ("cubrid_statdump module register-0\n");
  plugin_register_config (PLUGIN_NAME, config, config_keys, config_keys_num);
  plugin_register_init (PLUGIN_NAME, cubrid_statdump_init);
  plugin_register_read (PLUGIN_NAME, cubrid_statdump_read);
  plugin_register_shutdown (PLUGIN_NAME, cubrid_statdump_shutdown);
}
