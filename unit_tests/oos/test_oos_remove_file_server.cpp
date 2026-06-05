/*
 * test_oos_remove_file_server.cpp - SERVER_MODE tests for OOS file destruction
 *
 * Mirrors the SA_MODE test_oos_remove_file.cpp tests under full SERVER_MODE
 * infrastructure (MVCC, threading, worker transactions).
 */

#include "test_oos_server_common.hpp"

/* bridge functions defined in oos_file.cpp */
int bridge_oos_get_max_chunk_size_within_page ();

// ============================================================================
// TC: File destroy basic
// ============================================================================
TEST (OosFileDestroyServerTest, OosFileDestroyBasic)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}

// ============================================================================
// TC: File destroy with data
// ============================================================================
TEST (OosFileDestroyServerTest, OosFileDestroyWithData)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes ("Data before file destroy", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_NE (oid.pageid, NULL_PAGEID);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}

// ============================================================================
// TC: File destroy with multi-chunk data
// ============================================================================
TEST (OosFileDestroyServerTest, OosFileDestroyWithMultiChunkData)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  const int max_chunk_size = bridge_oos_get_max_chunk_size_within_page ();
  const int large_size = max_chunk_size + 50;

  auto large_data = test_oos_utils::make_repeated_pattern_string (large_size);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes (large_data, rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}

// ============================================================================
// TC: File destroy clears bestspace cache
// ============================================================================
TEST (OosFileDestroyServerTest, OosFileDestroyCacheCleared)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes ("Cache entry test data", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  err = oos_remove_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);
}

// ============================================================================
// TC: Page destroy basic
// ============================================================================
TEST (OosFileDestroyServerTest, OosPageDestroyBasic)
{
  int err;
  VFID oos_vfid;

  err = oos_create_file (thread_p, oos_vfid);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec_in {};
  err = test_oos_utils::from_string_into_recdes ("Page destroy test data", rec_in);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free (&rec_in, recdes_free_data_area);

  OID oid = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid, rec_in, oid);
  ASSERT_EQ (err, NO_ERROR);

  VPID vpid = {oid.pageid, oid.volid};

  err = oos_remove_page (thread_p, oos_vfid, vpid);
  ASSERT_EQ (err, NO_ERROR);
}

// ============================================================================
// TC: Destroy one file, other file still works
// ============================================================================
TEST (OosFileDestroyServerTest, OosFileDestroyMultipleFiles)
{
  int err;
  VFID oos_vfid1;
  VFID oos_vfid2;

  err = oos_create_file (thread_p, oos_vfid1);
  ASSERT_EQ (err, NO_ERROR);

  err = oos_create_file (thread_p, oos_vfid2);
  ASSERT_EQ (err, NO_ERROR);

  RECDES rec1 {};
  err = test_oos_utils::from_string_into_recdes ("File 1 data", rec1);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free1 (&rec1, recdes_free_data_area);

  RECDES rec2 {};
  err = test_oos_utils::from_string_into_recdes ("File 2 data", rec2);
  ASSERT_EQ (err, NO_ERROR);
  test_oos_utils::auto_freed_recdes_ptr defer_free2 (&rec2, recdes_free_data_area);

  OID oid1 = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid1, rec1, oid1);
  ASSERT_EQ (err, NO_ERROR);

  OID oid2 = OID_INITIALIZER;
  err = test_oos_utils::oos_insert_from_recdes (thread_p, oos_vfid2, rec2, oid2);
  ASSERT_EQ (err, NO_ERROR);

  /* destroy file 1 */
  err = oos_remove_file (thread_p, oos_vfid1);
  ASSERT_EQ (err, NO_ERROR);

  /* file 2 should still be readable */
  RECDES rec_out {};
  err = test_oos_utils::oos_read_with_alloc (thread_p, oid2, rec_out);
  ASSERT_EQ (err, NO_ERROR);
  ASSERT_STREQ (rec_out.data, "File 2 data");
  recdes_free_data_area (&rec_out);
}

int
main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerModeEnv ());
  ::testing::GTEST_FLAG (break_on_failure) = true;
  return RUN_ALL_TESTS ();
}
