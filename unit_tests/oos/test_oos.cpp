#include "gtest/gtest.h"

#include "dbi.h"
#include "thread_manager.hpp"
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

TEST (OosTest, OosCreateAndDestroy)
{
  auto error = db_restart ("unit_test", TRUE, "testdb");
  EXPECT_EQ (error, NO_ERROR);
  auto thread_p = thread_get_thread_entry_info();
  EXPECT_NE (thread_p, nullptr);

  int err;
  HFID hfid{};

  VFID oos_vfid;
  err = oos_create (thread_p, hfid, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  auto [fileid, volid] = oos_vfid;

  printf ("oos_vfid: fileid=%d, volid=%d\n", fileid, volid);

  EXPECT_NE (fileid, 0);
  EXPECT_EQ (fileid, 4288);
  EXPECT_NE (fileid, NULL_FILEID);
  EXPECT_NE (volid, NULL_VOLID);

  err = oos_destroy (thread_p, oos_vfid);
  auto [fileid_after_destroy, volid_after_destroy] = oos_vfid;

  EXPECT_EQ (err, NO_ERROR);
  EXPECT_EQ (fileid_after_destroy, NULL_FILEID);
  EXPECT_EQ (volid_after_destroy, NULL_VOLID);

  err = db_shutdown();
  EXPECT_EQ (err, NO_ERROR);
}

TEST (OosTest, OosCreateAndCreateAgain)
{
  auto error = db_restart ("unit_test", TRUE, "testdb");
  EXPECT_EQ (error, NO_ERROR);
  auto thread_p = thread_get_thread_entry_info();
  EXPECT_NE (thread_p, nullptr);

  int err;
  HFID common_hfid{};

  VFID oos_vfid;
  err = oos_create (thread_p, common_hfid, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  VFID oos_vfid2;
  err = oos_create (thread_p, common_hfid, oos_vfid2);
  EXPECT_EQ (err, NO_ERROR); // TODO: this should return error

  auto [fileid1, volid1] = oos_vfid;
  auto [fileid2, volid2] = oos_vfid2;

  printf ("First oos_vfid: fileid=%d, volid=%d\n", fileid1, volid1);
  printf ("Second oos_vfid: fileid=%d, volid=%d\n", fileid2, volid2);

  // either volid is different or fileid is different
  EXPECT_TRUE ( (fileid1 != fileid2) || (volid1 != volid2) );

  // TODO: if given hfid is identical, should return error
  // EXPECT_EQ (oos_create_err_2, ER_OOS_FILE_ALREADY_EXISTS);

  err = db_shutdown();
  EXPECT_EQ (err, NO_ERROR);

}

TEST (OosTest, OosInsertAndGet)
{
  auto error = db_restart ("unit_test", TRUE, "testdb");
  EXPECT_EQ (error, NO_ERROR);
  auto thread_p = thread_get_thread_entry_info();
  EXPECT_NE (thread_p, nullptr);

  int err;
  HFID hfid{};
  VFID oos_vfid;

  err = oos_create (thread_p, hfid, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  RECDES rec;
  err = recdes_allocate_data_area (&rec, 100);
  EXPECT_EQ (err, NO_ERROR);

  strncpy (&rec.data[0], "This is a test OOS record.", 100 - 1);
  rec.length = strlen (&rec.data[0]) + 1;

  OID oid;
  err = oos_insert (thread_p, oos_vfid, rec, oid);
  EXPECT_EQ (err, NO_ERROR);

  RECDES result_recdes;
  err = oos_read (thread_p, oos_vfid, oid, result_recdes);
  EXPECT_EQ (err, NO_ERROR);

  // TODO: EXPECT_EQ (result_recdes.length, rec.length);
  // TODO: EXPECT_STREQ (result_recdes.data, rec.data);

  err = db_shutdown();
  EXPECT_EQ (err, NO_ERROR);
}

TEST (OosTest, OosInsertLargerThanPageSize)
{
  auto error = db_restart ("unit_test", TRUE, "testdb");
  EXPECT_EQ (error, NO_ERROR);
  auto thread_p = thread_get_thread_entry_info();
  EXPECT_NE (thread_p, nullptr);

  int err;
  HFID hfid{};
  VFID oos_vfid;

  err = oos_create (thread_p, hfid, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  // TODO: // constexpr int larger_than_page_size = DB_PAGESIZE * 1.5;
  constexpr int larger_than_page_size = 5;

  RECDES rec;
  err = recdes_allocate_data_area (&rec, larger_than_page_size);
  EXPECT_EQ (err, NO_ERROR);

  const auto large_data = std::string (larger_than_page_size - 1, 'A');
  rec.length = larger_than_page_size;
  rec.data[rec.length - 1] = '\0';
  strncpy (rec.data, large_data.c_str(), larger_than_page_size - 1);
  EXPECT_EQ (strlen (rec.data), rec.length - 1);
  EXPECT_STREQ (rec.data, large_data.c_str());

  OID oid;
  err = oos_insert (thread_p, oos_vfid, rec, oid);
  EXPECT_EQ (err, NO_ERROR);

  RECDES result_recdes;
  err = oos_read (thread_p, oos_vfid, oid, result_recdes);
  EXPECT_EQ (err, NO_ERROR);
  EXPECT_STREQ (result_recdes.data, rec.data);
  EXPECT_STREQ (result_recdes.data, large_data.c_str());

  err = db_shutdown();
  EXPECT_EQ (err, NO_ERROR);
}

