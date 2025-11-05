#include "gtest/gtest.h"
#include <cstdio>

#include "dbi.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "thread_manager.hpp"
#include "oos_file.hpp"

cubthread::entry *thread_p;

std::string generate_large_string (int size)
{
  const std::string pattern = "ABCDEFGHIJ";
  if (size <= 0)
    return {};

  std::string large_data;
  large_data.reserve (size); // reserve full size, not size - 1

  for (int i = 0; i < size; ++i)
    {
      large_data.push_back (pattern[i % pattern.size()]);
    }

  return large_data;
}

int generate_record_from_string (const std::string &large_data, RECDES &rec)
{
  int err = recdes_allocate_data_area (&rec, static_cast<int> (large_data.size() + 1));
  if (err != NO_ERROR)
    {
      return err;
    }

  rec.type = REC_HOME;
  rec.length = static_cast<int> (large_data.size() + 1);

  // copy data including null terminator
  std::memcpy (rec.data, large_data.c_str(), large_data.size() + 1);
  return NO_ERROR;
}


TEST (BasicTest, Hello)
{
  EXPECT_STRNE ("Hello", "World");
  EXPECT_EQ (7 * 6, 42);
}

TEST (OosTest, OosCreateAndDestroy)
{
  int err;

  VFID oos_vfid;
  err = oos_create (thread_p, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  auto [fileid, volid] = oos_vfid;

  printf ("oos_vfid: fileid=%d, volid=%d\n", fileid, volid);
  EXPECT_NE (fileid, NULL_FILEID);
  EXPECT_NE (volid, NULL_VOLID);

  err = oos_destroy (thread_p, oos_vfid);
  auto [fileid_after_destroy, volid_after_destroy] = oos_vfid;

  EXPECT_EQ (err, NO_ERROR);
  // EXPECT_EQ (fileid_after_destroy, NULL_FILEID);
  // EXPECT_EQ (volid_after_destroy, NULL_VOLID);

}

TEST (OosTest, OosCreateAndCreateAgain)
{
  int err;

  VFID oos_vfid;
  err = oos_create (thread_p, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  VFID oos_vfid2;
  err = oos_create (thread_p, oos_vfid2);
  EXPECT_EQ (err, NO_ERROR);

  auto [fileid1, volid1] = oos_vfid;
  auto [fileid2, volid2] = oos_vfid2;

  printf ("First oos_vfid: fileid=%d, volid=%d\n", fileid1, volid1);
  printf ("Second oos_vfid: fileid=%d, volid=%d\n", fileid2, volid2);

  // either volid is different or fileid is different
  EXPECT_TRUE ( (fileid1 != fileid2) || (volid1 != volid2) );

}

TEST (OosTest, OosInsertAndRead)
{
  int err;
  VFID oos_vfid;

  err = oos_create (thread_p, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  RECDES rec{};
  const std::string random_data = "This is a test OOS data.";
  generate_record_from_string ("This is a test OOS data.", rec);

  OID oid = OID_INITIALIZER;
  err = oos_insert (thread_p, oos_vfid, rec, oid);
  EXPECT_EQ (err, NO_ERROR);
  EXPECT_NE (oid.pageid, NULL_PAGEID);
  EXPECT_NE (oid.volid, NULL_VOLID);
  EXPECT_NE (oid.slotid, NULL_SLOTID);
  printf ("OID: volid=%d, pageid=%d, slotid=%d\n", oid.volid, oid.pageid, oid.slotid);

  RECDES rec_out{};
  err = oos_read (thread_p, oos_vfid, oid, rec_out);
  EXPECT_EQ (err, NO_ERROR);

  EXPECT_EQ (rec_out.length, rec.length);
  EXPECT_STREQ (rec_out.data, rec.data);
  EXPECT_STREQ (rec_out.data, random_data.c_str());
}

TEST (OosTest, OosInsertLargerThanPageSize)
{
  int err;
  VFID oos_vfid;

  err = oos_create (thread_p, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  const int large_size = DB_PAGESIZE + 5;

  auto large_data = generate_large_string (large_size);

  RECDES rec_in{};
  err = generate_record_from_string (large_data, rec_in);
  EXPECT_EQ (err, NO_ERROR);

  OID oid;
  err = oos_insert (thread_p, oos_vfid, rec_in, oid);
  EXPECT_EQ (err, NO_ERROR);

  RECDES rec_out{};
  err = oos_read (thread_p, oos_vfid, oid, rec_out);
  EXPECT_EQ (err, NO_ERROR);
  EXPECT_STREQ (rec_out.data, rec_in.data);
  EXPECT_EQ (strlen (rec_out.data),strlen (large_data.c_str()));

  recdes_free_data_area (&rec_in);
  recdes_free_data_area (&rec_out);
  EXPECT_EQ (rec_in.data, nullptr);
  EXPECT_EQ (rec_out.data, nullptr);
}

TEST (OosTest, OosInsertAndRead100LargeSizesAroundPageSize)
{
  int err;
  VFID oos_vfid;

  err = oos_create (thread_p, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  const auto chunk_size = spage_max_record_size () - (int)sizeof (SPAGE_SLOT) - (int)sizeof (OOS_RECORD_HEADER);

  for (int large_size = chunk_size - 50; large_size <= chunk_size + 50; large_size++)
    {
      printf ("OosInsertAndReadLargeSizeAroundPageSize: testing large_size=%d\n", large_size);
      auto large_data = generate_large_string (large_size);

      RECDES rec_in{};
      err = generate_record_from_string (large_data, rec_in);
      EXPECT_EQ (err, NO_ERROR);

      OID oid;
      err = oos_insert (thread_p, oos_vfid, rec_in, oid);
      EXPECT_EQ (err, NO_ERROR);

      RECDES rec_out{};
      err = oos_read (thread_p, oos_vfid, oid, rec_out);
      EXPECT_EQ (err, NO_ERROR);
      EXPECT_EQ (strlen (rec_out.data),strlen (large_data.c_str()));
      EXPECT_STREQ (rec_out.data, rec_in.data);

      recdes_free_data_area (&rec_in);
      recdes_free_data_area (&rec_out);
      EXPECT_EQ (rec_in.data, nullptr);
      EXPECT_EQ (rec_out.data, nullptr);
    }

}

TEST (OosTest, OosFindBestSpace)
{
  int err;
  VFID oos_vfid;

  err = oos_create (thread_p, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  VPID vpid{};
  vpid.volid = NULL_VOLID;
  vpid.pageid = NULL_PAGEID;
  auto random_data_length = 100;
  err = oos_find_best_page (thread_p, oos_vfid, random_data_length, vpid);
  EXPECT_EQ (err, NO_ERROR);

  printf ("Best page found: volid=%d, pageid=%d\n", vpid.volid, vpid.pageid);
  EXPECT_NE (vpid.volid, NULL_VOLID);
  EXPECT_NE (vpid.pageid, NULL_PAGEID);
}

TEST (OosTest, OosFixAndUnfixPage)
{
  int err;
  VFID oos_vfid;

  err = oos_create (thread_p, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  const auto data1 = std::string ("this is a random data 1");

  VPID vpid{};
  vpid.volid = NULL_VOLID;
  vpid.pageid = NULL_PAGEID;
  auto random_data_length = 100;
  err = oos_find_best_page (thread_p, oos_vfid, random_data_length, vpid);
  EXPECT_EQ (err, NO_ERROR);

  printf ("Best page found: volid=%d, pageid=%d\n", vpid.volid, vpid.pageid);
  EXPECT_NE (vpid.volid, NULL_VOLID);
  EXPECT_NE (vpid.pageid, NULL_PAGEID);

  // manually initialize page
  PAGE_PTR page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  EXPECT_NE (page_ptr, nullptr);

  pgbuf_unfix (thread_p, page_ptr);
}

TEST (OosTest, OosManualSlottedPageInsertAndGet)
{
  int err;
  VFID oos_vfid;

  err = oos_create (thread_p, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  const auto data1 = std::string ("this is a random data 1");

  VPID vpid{};
  vpid.volid = NULL_VOLID;
  vpid.pageid = NULL_PAGEID;
  auto random_data_length = 100;
  err = oos_find_best_page (thread_p, oos_vfid, random_data_length, vpid);
  EXPECT_EQ (err, NO_ERROR);

  printf ("Best page found: volid=%d, pageid=%d\n", vpid.volid, vpid.pageid);
  EXPECT_NE (vpid.volid, NULL_VOLID);
  EXPECT_NE (vpid.pageid, NULL_PAGEID);

  // manual initialize page
  auto page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  EXPECT_NE (page_ptr, nullptr);

  // prepare insert data
  RECDES rec{};
  rec.type = REC_HOME;
  const auto insert_data = std::string ("this is a data to be inserted!");
  err = recdes_allocate_data_area (&rec, insert_data.size() + 1);
  EXPECT_EQ (err, NO_ERROR);

  std::memcpy (rec.data, insert_data.c_str(), insert_data.size() + 1);
  rec.length = static_cast<int> (insert_data.size() + 1);
  EXPECT_EQ (rec.length, insert_data.size() + 1);

  // read
  PGSLOTID slotid_out = -1;
  auto sp_error = spage_insert (thread_p, page_ptr, &rec, &slotid_out);
  EXPECT_EQ (sp_error, SP_SUCCESS);
  EXPECT_NE (slotid_out, -1);

  // prepare record to read data
  RECDES rec_out{};
  SCAN_CODE scan_code = spage_get_record (thread_p, page_ptr, slotid_out, &rec_out, PEEK);
  EXPECT_EQ (scan_code, S_SUCCESS);

  // see if rec and rec_out are same
  EXPECT_EQ (rec.length, rec_out.length);
  EXPECT_STREQ (rec.data, rec_out.data);

  pgbuf_unfix (thread_p, page_ptr);
  recdes_free_data_area (&rec);
  EXPECT_EQ (rec.data, nullptr);
}

TEST (OOS_TEST, ShouldInsertIntoSamePage)
{

  int err;
  VFID oos_vfid;

  err = oos_create (thread_p, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  const int large_size = DB_PAGESIZE + 5;

  RECDES rec_in1{};
  err = generate_record_from_string ("first string", rec_in1);
  EXPECT_EQ (err, NO_ERROR);

  RECDES rec_in2{};
  err = generate_record_from_string ("second string again", rec_in2);
  EXPECT_EQ (err, NO_ERROR);

  OID oid1;
  err = oos_insert (thread_p, oos_vfid, rec_in1, oid1);
  EXPECT_EQ (err, NO_ERROR);

  OID oid2;
  err = oos_insert (thread_p, oos_vfid, rec_in2, oid2);
  EXPECT_EQ (err, NO_ERROR);

  RECDES rec_out1{};
  err = oos_read (thread_p, oos_vfid, oid1, rec_out1);
  EXPECT_EQ (err, NO_ERROR);
  EXPECT_STREQ (rec_out1.data, rec_in1.data);
  EXPECT_EQ (strlen (rec_out1.data),strlen ("first string"));

  RECDES rec_out2{};
  err = oos_read (thread_p, oos_vfid, oid2, rec_out2);
  EXPECT_EQ (err, NO_ERROR);
  EXPECT_STREQ (rec_out2.data, rec_in2.data);
  EXPECT_EQ (strlen (rec_out2.data),strlen ("second string again"));

  // rec_out1 and rec_out2 should be in the same page
  EXPECT_EQ (oid1.pageid, oid2.pageid);
  EXPECT_EQ (oid1.volid, oid2.volid);

  recdes_free_data_area (&rec_in1);
  recdes_free_data_area (&rec_in2);
  recdes_free_data_area (&rec_out1);
  recdes_free_data_area (&rec_out2);

}

class ServerEnv : public ::testing::Environment
{
  public:
    void SetUp() override
    {
      StartServer();
    }
    void TearDown() override
    {
      StopServer();
    }
  private:
    void StartServer()
    {
      printf ("##### Starting Server #####\n");
      auto err = db_restart ("unit_test", TRUE, "testdb");
      EXPECT_EQ (err, NO_ERROR);
      thread_p = thread_get_thread_entry_info();
      EXPECT_NE (thread_p, nullptr);
    }
    void StopServer()
    {
      printf ("##### Stopping Server #####\n");
      auto err = db_shutdown();
      EXPECT_EQ (err, NO_ERROR);
      fflush (stdout);
    }
};

int main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerEnv());
  return RUN_ALL_TESTS();
}
