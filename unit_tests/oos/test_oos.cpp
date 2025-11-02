#include "gtest/gtest.h"
#include <gtest/gtest.h>

#include "test_oos.hpp"
#include "db_client_type.hpp"
#include "dbi.h"
#include "authenticate.h"
#include "util_support.h"
#include "thread_manager.hpp"
#include "xserver_interface.h"
#include "locator_sr.h"
#include "oos_file.hpp"


TEST (BasicTest, Hello)
{
  EXPECT_STRNE ("Hello", "World");
  EXPECT_EQ (7 * 6, 42);
}

TEST (BasicTest, Addition)
{
  int ret = 1 + 2;
  EXPECT_EQ (ret, 3);
}

TEST (OosTest, page_fix_unfix)
{
  auto db_name = "testdb";
  auto error = db_restart ("unit_test", TRUE, db_name);
  EXPECT_EQ (error, NO_ERROR);

  auto thread_p = thread_get_thread_entry_info();

  VPID vpid;
  vpid.volid = 0;
  vpid.pageid = 220;
  auto pgptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);

  printf ("Page fixed: volid=%d, pageid=%d\n", vpid.volid, vpid.pageid);
  printf ("pgptr=%p\n", pgptr);

  (void) pgbuf_check_page_ptype (thread_p, pgptr, PAGE_HEAP);
  printf ("Page type is PAGE_HEAP\n");
  fflush (stdout);

  HFID hfid;
  auto [x, y, z] = oos_init (thread_p, hfid);
  printf ("############## oos_init called\n");
  fflush (stdout);

  // EXPECT_EQ(1, 2);

  pgbuf_unfix (thread_p, pgptr);
  db_shutdown();

}

TEST (DiagdbTest, DiagdbMain)
{

  // UTIL_FUNCTION_ARG *arg = &util_func_arg;
  // UTIL_ARG_MAP *arg_map = arg->arg_map;
  char er_msg_file[PATH_MAX];
  const char *db_name;
  const char *output_file = NULL;
  const char *class_name;
  FILE *infp = NULL;
  FILE *outfp = NULL;
  bool is_emergency = false;
  bool need_db_shutdown = false;
  // DIAGDUMP_TYPE diag;
  THREAD_ENTRY *thread_p;
  int error_code = NO_ERROR;
  char *class_list_file;

  // db_name = utility_get_option_string_value (arg_map, OPTION_STRING_TABLE, 0);
  db_name = "testdb";
  if (db_name == NULL)
    {
      goto print_diag_usage;
    }

  // is_emergency = utility_get_option_bool_value (arg_map, DIAG_EMERGENCY_S);
  is_emergency = true;

  output_file = NULL;
  if (output_file == NULL)
    {
      outfp = stdout;
    }
  else
    {
      outfp = fopen (output_file, "w");
      if (outfp == NULL)
	{
	  goto error_exit;
	}
    }

  class_name = NULL;

  class_list_file = NULL;


  if (class_name && class_list_file)
    {
      fprintf (stderr, "The -n and -i options cannot be used together.\n");
      goto error_exit;
    }

  if (check_database_name (db_name))
    {
      goto error_exit;
    }

  /* error message log file */
  snprintf (er_msg_file, sizeof (er_msg_file) - 1, "%s_%s.err", db_name, "unittest");

  er_init (er_msg_file, ER_NEVER_EXIT);

  AU_DISABLE_PASSWORDS();
  db_set_client_type (DB_CLIENT_TYPE_ADMIN_UTILITY);
  db_login ("DBA", NULL);
  if (db_restart ("unit_test", TRUE, db_name) != NO_ERROR)
    {
      PRINT_AND_LOG_ERR_MSG ("%s\n", db_error_string (3));
      goto error_exit;
    }

  need_db_shutdown = true;


  thread_p = thread_get_thread_entry_info();

  VFID vfid;
  VPID vpid;
  HFID hfid;
  // heap_ovf_find_vfid (thread_p, &hfid, &vfid, true, PGBUF_UNCONDITIONAL_LATCH);

  db_shutdown();

  fflush (outfp);
  if (output_file != NULL && outfp != NULL && outfp != stdout)
    {
      fclose (outfp);
    }
  if (infp != NULL)
    {
      fclose (infp);
    }

  return;

print_diag_usage:
  fprintf (stderr, "wow");
  util_log_write_errid (MSGCAT_UTIL_GENERIC_INVALID_ARGUMENT);

error_exit:
  if (need_db_shutdown)
    {
      db_shutdown();
    }

  if (output_file != NULL && outfp != NULL && outfp != stdout)
    {
      fclose (outfp);
    }
  if (infp != NULL)
    {
      fclose (infp);
    }

}

