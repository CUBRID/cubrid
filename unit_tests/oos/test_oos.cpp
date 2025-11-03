#include "gtest/gtest.h"
#include <gtest/gtest.h>

#include "db_client_type.hpp"
#include "dbi.h"
#include "authenticate.h"
#include "thread_manager.hpp"
#include "page_buffer.h"
#include "oos_file.hpp"
#include "utility.h"


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

TEST (OosTest, OosCreate)
{
  auto error = db_restart ("unit_test", TRUE, "testdb");
  EXPECT_EQ (error, NO_ERROR);
  auto thread_p = thread_get_thread_entry_info();
  EXPECT_NE (thread_p, nullptr);

  HFID hfid{};
  auto [oos_create_err, oos_vfid] = oos_create (thread_p, hfid);
  EXPECT_EQ (oos_create_err, NO_ERROR);

  auto [fileid, volid] = oos_vfid;

  printf ("oos_vfid: fileid=%d, volid=%d\n", fileid, volid);

  EXPECT_NE (fileid, 0);
  EXPECT_EQ (fileid, 4288);
  EXPECT_NE (fileid, NULL_FILEID);
  EXPECT_NE (volid, NULL_VOLID);

}

TEST (OosTest, OosCreateAndCreateAgain)
{
  auto error = db_restart ("unit_test", TRUE, "testdb");
  EXPECT_EQ (error, NO_ERROR);
  auto thread_p = thread_get_thread_entry_info();
  EXPECT_NE (thread_p, nullptr);

  HFID hfid{};
  auto [oos_create_err, oss_vfid] = oos_create (thread_p, hfid);
  EXPECT_EQ (oos_create_err, NO_ERROR);

  auto [oos_create_err_2,oos_vfid2] = oos_create (thread_p, hfid);

  auto [fileid1, volid1] = oss_vfid;
  auto [fileid2, volid2] = oos_vfid2;

  printf ("First oos_vfid: fileid=%d, volid=%d\n", fileid1, volid1);
  printf ("Second oos_vfid: fileid=%d, volid=%d\n", fileid2, volid2);

  // either volid is different or fileid is different
  EXPECT_TRUE ( (fileid1 != fileid2) || (volid1 != volid2) );

  // TODO: if given hfid is identical, should return error
  // EXPECT_EQ (oos_create_err_2, ER_OOS_FILE_ALREADY_EXISTS);

}

TEST (OosTest, OosCreateAndDestroy)
{
  auto error = db_restart ("unit_test", TRUE, "testdb");
  EXPECT_EQ (error, NO_ERROR);
  auto thread_p = thread_get_thread_entry_info();
  EXPECT_NE (thread_p, nullptr);

  HFID hfid{};
  auto [oos_create_err, oos_vfid] = oos_create (thread_p, hfid);
  EXPECT_EQ (oos_create_err, NO_ERROR);

  // TODO: oos_destroy should work under recovery
  // auto ret = oos_destroy (thread_p, oos_vfid);
  // EXPECT_EQ (ret, NO_ERROR);
}

TEST (OosTest, OosInsertAndGet)
{
  auto error = db_restart ("unit_test", TRUE, "testdb");
  EXPECT_EQ (error, NO_ERROR);
  auto thread_p = thread_get_thread_entry_info();
  EXPECT_NE (thread_p, nullptr);

  auto [oos_create_err, oos_vfid] = oos_create (thread_p, * (new HFID()));
  EXPECT_EQ (oos_create_err, NO_ERROR);

  auto inserted_recs = std::vector<RECDES> {};
  auto [oos_insert_err, oos_recdeses ] = oos_insert (thread_p, oos_vfid, inserted_recs);

  EXPECT_EQ (oos_insert_err, NO_ERROR);
  EXPECT_EQ (oos_recdeses.size(), inserted_recs.size());

  auto first_oid = OID{};
  auto result_recdes = RECDES{};
  auto error_code = oos_get (thread_p, oos_vfid, first_oid, result_recdes);
  oos_get (thread_p, oos_vfid, first_oid, result_recdes);

  EXPECT_EQ (error_code, NO_ERROR);
}


// TEST (OosTest, PrintOos)
// {
//   auto error = db_restart ("unit_test", TRUE, "testdb");
//   EXPECT_EQ (error, NO_ERROR);
//   auto thread_p = thread_get_thread_entry_info();
//   EXPECT_NE (thread_p, nullptr);
// }

TEST (OosTest, PageFixUnfix)
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
  auto [x, y] = oos_create (thread_p, hfid);
  printf ("############## oos_init called\n");
  fflush (stdout);

  // EXPECT_EQ(1, 2);

  pgbuf_unfix (thread_p, pgptr);
  db_shutdown();

}

