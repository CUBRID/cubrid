#include "gtest/gtest.h"
#include <cstdio>

#include "dbi.h"
#include "thread_manager.hpp"
#include "oos_file.hpp"

cubthread::entry *thread_p;

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
  EXPECT_EQ (fileid_after_destroy, NULL_FILEID);
  EXPECT_EQ (volid_after_destroy, NULL_VOLID);

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

TEST (OosTest, OosInsertAndGet)
{
  int err;
  VFID oos_vfid;

  err = oos_create (thread_p, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  RECDES rec;
  err = recdes_allocate_data_area (&rec, 100);
  EXPECT_EQ (err, NO_ERROR);

  strncpy (&rec.data[0], "This is a test OOS record.", 100 - 1);
  rec.length = strlen (&rec.data[0]) + 1;

  OID oid;
  err = oos_insert (thread_p, oos_vfid, rec, oid);
  EXPECT_EQ (err, NO_ERROR);

  RECDES result_recdes{};
  err = oos_read (thread_p, oos_vfid, oid, result_recdes);
  EXPECT_EQ (err, NO_ERROR);

  // TODO: EXPECT_EQ (result_recdes.length, rec.length);
  // TODO: EXPECT_STREQ (result_recdes.data, rec.data);

}

TEST (OosTest, OosInsertLargerThanPageSize)
{
  int err;
  VFID oos_vfid;

  err = oos_create (thread_p, oos_vfid);
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

  RECDES result_recdes{};
  err = oos_read (thread_p, oos_vfid, oid, result_recdes);
  EXPECT_EQ (err, NO_ERROR);
  EXPECT_STREQ (result_recdes.data, rec.data);
  EXPECT_STREQ (result_recdes.data, large_data.c_str());

}

TEST (OosTest, OosFindBestSpace)
{
  int err;
  VFID oos_vfid;

  err = oos_create (thread_p, oos_vfid);
  EXPECT_EQ (err, NO_ERROR);

  const auto data1 = std::string ("this is a random data 1");

  RECDES rec;
  auto rec_length = data1.size() + 1;
  err = recdes_allocate_data_area (&rec, rec_length);
  EXPECT_EQ (err, NO_ERROR);
  rec.length = rec_length;
  rec.data[rec.length - 1] = '\0';
  strncpy (rec.data, data1.c_str(), rec_length - 1);
  EXPECT_EQ (strlen (rec.data), rec.length - 1);
  EXPECT_STREQ (rec.data, data1.c_str());

  VPID vpid{};
  err = oos_find_best_page (thread_p, oos_vfid, rec.length, vpid);
  EXPECT_EQ (err, NO_ERROR);

  printf ("Best page found: volid=%d, pageid=%d\n", vpid.volid, vpid.pageid);

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
      fflush(stdout);
    }
};

int main (int argc, char **argv)
{
  ::testing::InitGoogleTest (&argc, argv);
  ::testing::AddGlobalTestEnvironment (new ServerEnv());
  return RUN_ALL_TESTS();
}
