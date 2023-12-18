/*
 * Copyright 2008 Search Solution Corporation
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

#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"

#include "atomic_replication_helper.hpp"

#include "fake_packable_object.hpp"

// ****************************************************************
// test_spec_type declaration
// ****************************************************************

// NOTE: [temporarily] deactivate testing of fail to fix pages as there is no straightforward
//  method of implementing the check after the most recent changes in the implementation
//#define UTEST_LOG_REPL_ATOMIC_TEST_FIX_FAIL

// invalid values
constexpr VPID INV_VPID { NULL_PAGEID, NULL_VOLID };
constexpr LOG_RECTYPE INV_RECTYPE = LOG_SMALLER_LOGREC_TYPE;
constexpr LOG_RCVINDEX INV_RCVINDEX = RV_NOT_DEFINED;
constexpr LOG_SYSOP_END_TYPE INV_SYSOP_END_TYPE = (LOG_SYSOP_END_TYPE)-1;
constexpr LOG_LSA INV_LSA { NULL_LOG_PAGEID, NULL_LOG_OFFSET };

constexpr bool FIX_SUCC = true;
constexpr bool FIX_FAIL = false;

struct fake_system_parameters_initialize_type
{
  fake_system_parameters_initialize_type ();
};

struct log_record_spec_type
{
  TRANID m_trid;
  LOG_LSA m_lsa;
  VPID m_vpid;
  LOG_RECTYPE m_rectype;
  LOG_RCVINDEX m_rcvindex;
  LOG_SYSOP_END_TYPE m_sysop_end_type;
  LOG_LSA m_sysop_end_last_parent_lsa;

  // whether the page for the log record will be fixed or will
  // faill to be fixed (to simulate real life conditions)
  bool m_fix_success;

  bool m_severe_error_occured = false;
};
using log_record_spec_vector_type = std::vector<log_record_spec_type>;

using vpid_page_ptr_map = std::map<VPID, PAGE_PTR>;
using page_ptr_set = std::set<PAGE_PTR>;

struct test_spec_type
{
    test_spec_type ();

    test_spec_type (const test_spec_type &) = delete;
    test_spec_type (test_spec_type &&) = delete;

    ~test_spec_type ();

    test_spec_type &operator = (const test_spec_type &) = delete;
    test_spec_type &operator = (test_spec_type &&) = delete;

    void calculate_log_records_offsets (LOG_LSA start_lsa);

    void execute (cublog::atomic_replication_helper &atomic_helper);

    void check_after_execution (TRANID trid, const cublog::atomic_replication_helper &atomic_helper);

    PAGE_PTR alloc_page (const VPID &vpid);
    PAGE_PTR fix_page (const VPID &vpid);
    void unfix_page (PAGE_PTR page_ptr);

    void check_error (int severity, int err_id);

  public:
    // RAII style, make sure it is the first member of the class
    // to ensure that the initialization gets executed before anything else
    fake_system_parameters_initialize_type m_system_params;

    THREAD_ENTRY *m_thread_p = nullptr;

    log_rv_redo_context m_log_redo_context;

    // the actual log record sequence that the test is executing
    log_record_spec_vector_type m_log_record_vec;
#ifdef UTEST_LOG_REPL_ATOMIC_TEST_FIX_FAIL
    // points to the current replicating log
    log_record_spec_type *m_current_log_ptr = nullptr;
#endif

    // bookkeeping for pgbuf functionality
    vpid_page_ptr_map m_fixed_page_map;
    page_ptr_set m_fixed_page_ptr_set;
};

// global test spec instance to be used by bookkeeping by mocked functionality
test_spec_type *gl_Test_Spec = nullptr;

// ****************************************************************
// actual tests
// ****************************************************************

TEST_CASE ("LOG_START/END_ATOMIC_REPL", "")
{
  // logging snippet:
  //  _CL_ LSA = 10177|5376  rectype = LOG_START_ATOMIC_REPL  sysop_end_type = N_A  sysop_end_last_parent_lsa = -1|-1
  //  _FL_ LSA = 10177|5408  vpid = 0|8842  rcvindex = RVHF_UPDATE_NOTIFY_VACUUM
  //  _FL_ LSA = 10177|5816  vpid = 0|8842  rcvindex = RVHF_SET_PREV_VERSION_LSA
  //  _CL_ LSA = 10177|5872  rectype = LOG_END_ATOMIC_REPL  sysop_end_type = N_A  sysop_end_last_parent_lsa = -1|-1

  test_spec_type test_spec;

  log_record_spec_vector_type &log_rec_vec = test_spec.m_log_record_vec;
  constexpr TRANID trid = 5;
  constexpr LOG_LSA start_lsa = { 77, 7 };
  log_rec_vec =
  {
    { trid, INV_LSA, INV_VPID, LOG_START_ATOMIC_REPL, INV_RCVINDEX, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 102, 0 }, INV_RECTYPE, RVHF_UPDATE_NOTIFY_VACUUM, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 102, 0 }, INV_RECTYPE, RVHF_SET_PREV_VERSION_LSA, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, INV_VPID, LOG_END_ATOMIC_REPL, INV_RCVINDEX, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
  };

  test_spec.calculate_log_records_offsets (start_lsa);

  gl_Test_Spec = &test_spec;

  cublog::atomic_replication_helper atomic_helper;
  test_spec.execute (atomic_helper);
  test_spec.check_after_execution (trid, atomic_helper);
}

TEST_CASE ("LOG_SYSOP_ATOMIC_START/LOG_SYSOP_END-LOG_SYSOP_END_LOGICAL_UNDO", "")
{
  // logging snippet:
  // _CL_ LSA = 340|3880  rectype = LOG_SYSOP_ATOMIC_START  sysop_end_type = N_A  sysop_end_last_parent_lsa = -1|-1
  // _WK_ LSA = 340|3912  vpid = 0|4160  rcvindex = RVFL_PARTSECT_ALLOC
  // _WK_ LSA = 340|3984  vpid = 0|4160  rcvindex = RVFL_FHEAD_ALLOC
  // _WK_ LSA = 340|4056  vpid = 0|4160  rcvindex = RVFL_EXTDATA_REMOVE
  // _WK_ LSA = 340|4144  vpid = 0|4160  rcvindex = RVFL_EXTDATA_ADD
  // _FL_ LSA = 340|4224  vpid = 0|4351  rcvindex = RVHF_NEWPAGE
  // _CL_ LSA = 340|4320  rectype = LOG_SYSOP_END  sysop_end_type = LOG_SYSOP_END_LOGICAL_UNDO  sysop_end_last_parent_lsa = 340|3880

  test_spec_type test_spec;

  log_record_spec_vector_type &log_rec_vec = test_spec.m_log_record_vec;
  constexpr TRANID trid = 5;
  constexpr LOG_LSA start_lsa = { 40, 11 };
  log_rec_vec =
  {
    { trid, INV_LSA, INV_VPID, LOG_SYSOP_ATOMIC_START, INV_RCVINDEX, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 60, 0 }, INV_RECTYPE, RVFL_PARTSECT_ALLOC, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 60, 0 }, INV_RECTYPE, RVFL_FHEAD_ALLOC, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 60, 0 }, INV_RECTYPE, RVFL_EXTDATA_REMOVE, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 60, 0 }, INV_RECTYPE, RVFL_EXTDATA_ADD, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 51, 0 }, INV_RECTYPE, RVHF_NEWPAGE, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
  };
  test_spec.calculate_log_records_offsets (start_lsa);
  const LOG_LSA &sysop_end_last_parent_lsa = log_rec_vec[0].m_lsa;
  log_rec_vec.push_back (
  {
    trid, INV_LSA, INV_VPID, LOG_SYSOP_END, INV_RCVINDEX, LOG_SYSOP_END_LOGICAL_UNDO,
    sysop_end_last_parent_lsa, FIX_SUCC });

  test_spec.calculate_log_records_offsets (start_lsa);

  gl_Test_Spec = &test_spec;

  cublog::atomic_replication_helper atomic_helper;
  test_spec.execute (atomic_helper);
  test_spec.check_after_execution (trid, atomic_helper);
}

TEST_CASE ("LOG_SYSOP_ATOMIC_START/LOG_SYSOP_END-LOG_SYSOP_END_COMMIT", "")
{
  // logging snippet:
  // _CL_ LSA = 239|16128  rectype = LOG_SYSOP_ATOMIC_START  sysop_end_type = N_A  sysop_end_last_parent_lsa = -1|-1
  // _WK_ LSA = 239|16160  vpid = 0|1  rcvindex = RVDK_RESERVE_SECTORS
  // _WK_ LSA = 239|16232  vpid = 0|4160  rcvindex = RVFL_EXPAND
  // _CL_ LSA = 239|16296  rectype = LOG_SYSOP_END  sysop_end_type = LOG_SYSOP_END_COMMIT  sysop_end_last_parent_lsa = 239|16128

  test_spec_type test_spec;

  log_record_spec_vector_type &log_rec_vec = test_spec.m_log_record_vec;
  constexpr TRANID trid = 5;
  constexpr LOG_LSA start_lsa = { 39, 11111 };
  log_rec_vec =
  {
    { trid, INV_LSA, INV_VPID, LOG_SYSOP_ATOMIC_START, INV_RCVINDEX, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 1, 0 }, INV_RECTYPE, RVDK_RESERVE_SECTORS, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 60, 0 }, INV_RECTYPE, RVFL_EXPAND, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
  };
  test_spec.calculate_log_records_offsets (start_lsa);
  const LOG_LSA &sysop_end_last_parent_lsa = log_rec_vec[0].m_lsa;
  log_rec_vec.push_back (
  { trid, INV_LSA, INV_VPID, LOG_SYSOP_END, INV_RCVINDEX, LOG_SYSOP_END_COMMIT, sysop_end_last_parent_lsa, FIX_SUCC });

  test_spec.calculate_log_records_offsets (start_lsa);

  gl_Test_Spec = &test_spec;

  cublog::atomic_replication_helper atomic_helper;
  test_spec.execute (atomic_helper);
  test_spec.check_after_execution (trid, atomic_helper);
}

TEST_CASE ("LOG_SYSOP_ATOMIC_START/LOG_SYSOP_START_POSTPONE/LOG_SYSOP_END", "")
{
  // _CL_ LSA = 32132|2976  rectype = LOG_SYSOP_ATOMIC_START  sysop_end_type = N_A  sysop_end_last_parent_lsa = -1|-1
  // _FL_ LSA = 32132|7184  vpid = 0|11915  rcvindex = RVBT_NDRECORD_DEL
  // _FL_ LSA = 32132|7272  vpid = 0|5388  rcvindex = RVBT_COPYPAGE
  // _FL_ LSA = 32132|14048  vpid = 0|9964  rcvindex = RVBT_NDHEADER_UPD
  // _CL_ LSA = 32132|14192  rectype = LOG_SYSOP_START_POSTPONE  sysop_end_type = N_A  sysop_end_last_parent_lsa = -1|-1
  // _CL_ LSA = 32132|14312  rectype = LOG_SYSOP_ATOMIC_START  sysop_end_type = N_A  sysop_end_last_parent_lsa = -1|-1
  // _WK_ LSA = 32132|14344  vpid = 0|4224  rcvindex = RVFL_EXTDATA_REMOVE
  // _WK_ LSA = 32132|14424  vpid = 0|4224  rcvindex = RVFL_EXTDATA_ADD
  // _WK_ LSA = 32132|14512  vpid = 0|4224  rcvindex = RVFL_FHEAD_DEALLOC
  // _FL_ LSA = 32132|14584  vpid = 0|4271  rcvindex = RVPGBUF_DEALLOC
  // _CL_ LSA = 32132|14648  rectype = LOG_SYSOP_END  sysop_end_type = LOG_SYSOP_END_LOGICAL_RUN_POSTPONE  sysop_end_last_parent_lsa = 32132|14192
  // _CL_ LSA = 32132|14760  rectype = LOG_SYSOP_END  sysop_end_type = LOG_SYSOP_END_COMMIT  sysop_end_last_parent_lsa = -1|-1

  test_spec_type test_spec;

  log_record_spec_vector_type &log_rec_vec = test_spec.m_log_record_vec;
  constexpr TRANID trid = 5;
  constexpr LOG_LSA start_lsa = { 32, 76 };
  log_rec_vec =
  {
    { trid, INV_LSA, INV_VPID, LOG_SYSOP_ATOMIC_START, INV_RCVINDEX, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 15, 0 }, INV_RECTYPE, RVBT_NDRECORD_DEL, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 88, 0 }, INV_RECTYPE, RVBT_COPYPAGE, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 64, 0 }, INV_RECTYPE, RVBT_NDHEADER_UPD, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, INV_VPID, LOG_SYSOP_START_POSTPONE, INV_RCVINDEX, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, INV_VPID, LOG_SYSOP_ATOMIC_START, INV_RCVINDEX, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 24, 0 }, INV_RECTYPE, RVFL_EXTDATA_REMOVE, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 24, 0 }, INV_RECTYPE, RVFL_EXTDATA_ADD, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 24, 0 }, INV_RECTYPE, RVFL_FHEAD_DEALLOC, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 71, 0 }, INV_RECTYPE, RVPGBUF_DEALLOC, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
  };
  test_spec.calculate_log_records_offsets (start_lsa);
  // lsa of LOG_SYSOP_START_POSTPONE log record
  const LOG_LSA &sysop_end_last_parent_lsa = log_rec_vec[4].m_lsa;
  log_rec_vec.push_back (
  {
    trid, INV_LSA, INV_VPID, LOG_SYSOP_END, INV_RCVINDEX, LOG_SYSOP_END_LOGICAL_RUN_POSTPONE,
    sysop_end_last_parent_lsa, FIX_SUCC });
  log_rec_vec.push_back (
  { trid, INV_LSA, INV_VPID, LOG_SYSOP_END, INV_RCVINDEX, LOG_SYSOP_END_COMMIT, INV_LSA, FIX_SUCC });

  test_spec.calculate_log_records_offsets (start_lsa);

  gl_Test_Spec = &test_spec;

  cublog::atomic_replication_helper atomic_helper;
  test_spec.execute (atomic_helper);
  test_spec.check_after_execution (trid, atomic_helper);
}

TEST_CASE ("LOG_SYSOP_ATOMIC_START/LOG_SYSOP_END-LOG_SYSOP_END_LOGICAL_UNDO - fail to fix", "")
{
  // logging snippet:
  // _CL_ LSA = 340|3880  rectype = LOG_SYSOP_ATOMIC_START  sysop_end_type = N_A  sysop_end_last_parent_lsa = -1|-1
  // _WK_ LSA = 340|3912  vpid = 0|4160  rcvindex = RVFL_PARTSECT_ALLOC
  // _WK_ LSA = 340|3984  vpid = 0|4160  rcvindex = RVFL_FHEAD_ALLOC
  // _WK_ LSA = 340|4056  vpid = 0|4160  rcvindex = RVFL_EXTDATA_REMOVE
  // _WK_ LSA = 340|4144  vpid = 0|4160  rcvindex = RVFL_EXTDATA_ADD
  // _FL_ LSA = 340|4224  vpid = 0|4351  rcvindex = RVHF_NEWPAGE
  // _CL_ LSA = 340|4320  rectype = LOG_SYSOP_END  sysop_end_type = LOG_SYSOP_END_LOGICAL_UNDO  sysop_end_last_parent_lsa = 340|3880

  test_spec_type test_spec;

  log_record_spec_vector_type &log_rec_vec = test_spec.m_log_record_vec;
  constexpr TRANID trid = 5;
  constexpr LOG_LSA start_lsa = { 40, 11 };
  log_rec_vec =
  {
    { trid, INV_LSA, INV_VPID, LOG_SYSOP_ATOMIC_START, INV_RCVINDEX, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 60, 0 }, INV_RECTYPE, RVFL_PARTSECT_ALLOC, INV_SYSOP_END_TYPE, INV_LSA, FIX_FAIL },
    { trid, INV_LSA, { 60, 0 }, INV_RECTYPE, RVFL_FHEAD_ALLOC, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 60, 0 }, INV_RECTYPE, RVFL_EXTDATA_REMOVE, INV_SYSOP_END_TYPE, INV_LSA, FIX_FAIL },
    { trid, INV_LSA, { 60, 0 }, INV_RECTYPE, RVFL_EXTDATA_ADD, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 51, 0 }, INV_RECTYPE, RVHF_NEWPAGE, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
  };
  test_spec.calculate_log_records_offsets (start_lsa);
  const LOG_LSA &sysop_end_last_parent_lsa = log_rec_vec[0].m_lsa;
  log_rec_vec.push_back (
  {
    trid, INV_LSA, INV_VPID, LOG_SYSOP_END, INV_RCVINDEX, LOG_SYSOP_END_LOGICAL_UNDO,
    sysop_end_last_parent_lsa, FIX_SUCC });

  test_spec.calculate_log_records_offsets (start_lsa);

  gl_Test_Spec = &test_spec;

  cublog::atomic_replication_helper atomic_helper;
  test_spec.execute (atomic_helper);
  test_spec.check_after_execution (trid, atomic_helper);
}

//  _CL_ LSA = 35288|2848  rectype = LOG_SYSOP_ATOMIC_START  sysop_end_type = N_A  sysop_end_last_parent_lsa = -1|-1
//  _FL_ LSA = 35288|2880  vpid = 0|640  rcvindex = RVFL_PARTSECT_DEALLOC
//  _FL_ LSA = 35288|2952  vpid = 0|640  rcvindex = RVFL_FHEAD_DEALLOC
//  _FL_ LSA = 35288|3024  vpid = 0|641  rcvindex = RVPGBUF_DEALLOC
//  _CL_ LSA = 35288|3088  rectype = LOG_SYSOP_END  sysop_end_type = LOG_SYSOP_END_LOGICAL_RUN_POSTPONE  sysop_end_last_parent_lsa = 35288|2728

TEST_CASE ("LOG_SYSOP_ATOMIC_START/LOG_SYSOP_START_POSTPONE/LOG_SYSOP_END - 2 Transactions", "")
{
  // TRID - 3              TRID -8
  //
  //                       LSA = 105813|1488, Forw log = 105813|1520, Backw log = 105813|1416,
  //                            Trid =  -8, Prev tran logrec =  -1| -1
  //                            Type = LOG_SYSOP_ATOMIC_START
  //
  //                       LSA = 105813|1520, Forw log = 105813|1592, Backw log = 105813|1488,
  //                            Trid =  -8, Prev tran logrec = 105813|1488
  //                            Type = LOG_UNDOREDO_DATA, Recv_index = RVBT_RECORD_MODIFY_UNDOREDO,
  //                            Volid = 0 Pageid = 9933 Offset = -8192,
  //
  //                       LSA = 105813|1640, Forw log = 105813|1704, Backw log = 105813|1592,
  //                            Trid =  -8, Prev tran logrec = 105813|1520
  //                            Type = LOG_POSTPONE, Recv_index = RVFL_DEALLOC,
  //                            Volid = -1 Pageid = -1 Offset = 0,
  //
  // LSA = 105818|528, Forw log = 105818|560, Backw log = 105818|456,
  //      Trid =  -3, Prev tran logrec =  -1| -1
  //      Type = LOG_SYSOP_ATOMIC_START
  //
  // LSA = 105818|560, Forw log = 105818|632, Backw log = 105818|528,
  //      Trid =  -3, Prev tran logrec = 105818|528
  //      Type = LOG_UNDOREDO_DATA, Recv_index = RVBT_RECORD_MODIFY_UNDOREDO,
  //      Volid = 0 Pageid = 4241 Offset = -8192,
  //
  // LSA = 105818|776, Forw log = 105818|840, Backw log = 105818|704,
  //      Trid =  -3, Prev tran logrec = 105818|560
  //      Type = LOG_POSTPONE, Recv_index = RVFL_DEALLOC,
  //      Volid = -1 Pageid = -1 Offset = 0,
  //
  // LSA = 105818|1248, Forw log = 105818|1368, Backw log = 105818|1176,
  //      Trid =  -3, Prev tran logrec = 105818|776
  //      Type = LOG_SYSOP_START_POSTPONE,
  //      lastparent_lsa = -1|-1, prv_topresult_lsa = -1|-1, type = LOG_SYSOP_END_COMMIT
  //      posp_lsa = 105818|776
  //
  // LSA = 105818|1632, Forw log = 105818|1664, Backw log = 105818|1584,
  //      Trid =  -3, Prev tran logrec = 105818|1248
  //      Type = LOG_SYSOP_ATOMIC_START
  //
  // LSA = 105818|1664, Forw log = 105818|1736, Backw log = 105818|1632,
  //      Trid =  -3, Prev tran logrec = 105818|1632
  //      Type = LOG_UNDOREDO_DATA, Recv_index = RVFL_PARTSECT_DEALLOC,
  //      Volid = 0 Pageid = 4224 Offset = 232,
  //
  //                       LSA = 105818|1736, Forw log = 105818|1856, Backw log = 105818|1664,
  //                            Trid =  -8, Prev tran logrec = 105813|1640
  //                            Type = LOG_SYSOP_START_POSTPONE,
  //                            lastparent_lsa = -1|-1, prv_topresult_lsa = -1|-1, type = LOG_SYSOP_END_COMMIT
  //                            posp_lsa = 105813|1640
  //
  // LSA = 105818|1856, Forw log = 105818|1928, Backw log = 105818|1736,
  //      Trid =  -3, Prev tran logrec = 105818|1664
  //      Type = LOG_UNDOREDO_DATA, Recv_index = RVFL_FHEAD_DEALLOC,
  //      Volid = 0 Pageid = 4224 Offset = 0,
  //
  // LSA = 105818|1928, Forw log = 105818|1992, Backw log = 105818|1856,
  //      Trid =  -3, Prev tran logrec = 105818|1856
  //      Type = LOG_UNDOREDO_DATA, Recv_index = RVPGBUF_DEALLOC,
  //      Volid = 0 Pageid = 4240 Offset = 0,
  //
  // LSA = 105818|1992, Forw log = 105818|2104, Backw log = 105818|1928,
  //      Trid =  -3, Prev tran logrec = 105818|1928
  //      Type = LOG_SYSOP_END,
  //      lastparent_lsa = 105818|1248, prv_topresult_lsa = -1|-1, type = LOG_SYSOP_END_LOGICAL_RUN_POSTPONE
  //      run_postpone_lsa = 105818|776, postpone = sysop
  //
  //                       LSA = 105818|2416, Forw log = 105818|2448, Backw log = 105818|2344,
  //                            Trid =  -8, Prev tran logrec = 105818|1736
  //                            Type = LOG_SYSOP_ATOMIC_START
  //
  //                       LSA = 105818|2448, Forw log = 105818|2520, Backw log = 105818|2416,
  //                            Trid =  -8, Prev tran logrec = 105818|2416
  //                            Type = LOG_UNDOREDO_DATA, Recv_index = RVFL_PARTSECT_DEALLOC,
  //                            Volid = 0 Pageid = 4224 Offset = 264,
  //
  //                       LSA = 105818|2568, Forw log = 105818|2640, Backw log = 105818|2520,
  //                            Trid =  -8, Prev tran logrec = 105818|2448
  //                            Type = LOG_UNDOREDO_DATA, Recv_index = RVFL_FHEAD_DEALLOC,
  //                            Volid = 0 Pageid = 4224 Offset = 0,
  //
  //                       LSA = 105818|2760, Forw log = 105818|2824, Backw log = 105818|2688,
  //                            Trid =  -8, Prev tran logrec = 105818|2568
  //                            Type = LOG_UNDOREDO_DATA, Recv_index = RVPGBUF_DEALLOC,
  //                            Volid = 0 Pageid = 6725 Offset = 0,
  //
  //                       LSA = 105818|2824, Forw log = 105818|2936, Backw log = 105818|2760,
  //                            Trid =  -8, Prev tran logrec = 105818|2760
  //                            Type = LOG_SYSOP_END,
  //                            lastparent_lsa = 105818|1736, prv_topresult_lsa = -1|-1, type = LOG_SYSOP_END_LOGICAL_RUN_POSTPONE
  //                            run_postpone_lsa = 105813|1640, postpone = sysop
  //
  //                       LSA = 105818|3056, Forw log = 105818|3168, Backw log = 105818|2984,
  //                            Trid =  -8, Prev tran logrec = 105818|2824
  //                            Type = LOG_SYSOP_END,
  //                            lastparent_lsa = -1|-1, prv_topresult_lsa = -1|-1, type = LOG_SYSOP_END_COMMIT
  //
  // LSA = 105818|3216, Forw log = 105818|3328, Backw log = 105818|3168,
  //      Trid =  -3, Prev tran logrec = 105818|1992
  //      Type = LOG_SYSOP_END,
  //      lastparent_lsa = -1|-1, prv_topresult_lsa = -1|-1, type = LOG_SYSOP_END_COMMIT

  test_spec_type test_spec;

  log_record_spec_vector_type &log_rec_vec = test_spec.m_log_record_vec;

  constexpr TRANID trid1 = -3;
  constexpr TRANID trid2 = -8;
  constexpr LOG_LSA start_lsa = { 813, 488 };

  log_rec_vec =
  {
    { trid2, INV_LSA, INV_VPID, LOG_SYSOP_ATOMIC_START, INV_RCVINDEX, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid2, INV_LSA, { 933, 0 }, LOG_UNDOREDO_DATA, RVBT_RECORD_MODIFY_UNDOREDO, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
//  { trid2, INV_LSA, INV_VPID, LOG_POSTPONE, INV_RCVINDEX, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },

    { trid1, INV_LSA, INV_VPID, LOG_SYSOP_ATOMIC_START, INV_RCVINDEX, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid1, INV_LSA, { 241, 0 }, LOG_UNDOREDO_DATA, RVBT_RECORD_MODIFY_UNDOREDO, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
//  { trid1, INV_LSA, INV_VPID, LOG_POSTPONE, INV_RCVINDEX, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid1, INV_LSA, INV_VPID, LOG_SYSOP_START_POSTPONE, INV_RCVINDEX, LOG_SYSOP_END_COMMIT, INV_LSA, FIX_SUCC },
    { trid1, INV_LSA, INV_VPID, LOG_SYSOP_ATOMIC_START, INV_RCVINDEX, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid1, INV_LSA, { 224, 0 }, LOG_UNDOREDO_DATA, RVFL_PARTSECT_DEALLOC, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },

    { trid2, INV_LSA, INV_VPID, LOG_SYSOP_START_POSTPONE, INV_RCVINDEX, LOG_SYSOP_END_COMMIT, INV_LSA, FIX_SUCC },

    { trid1, INV_LSA, { 224, 0 }, LOG_UNDOREDO_DATA, RVFL_FHEAD_DEALLOC, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid1, INV_LSA, { 240, 0 }, LOG_UNDOREDO_DATA, RVPGBUF_DEALLOC, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
  };

  // trid1
  //
  test_spec.calculate_log_records_offsets (start_lsa);
  log_record_spec_type &trid1_sysop_start_postpone_rec = log_rec_vec[4];
  REQUIRE (trid1_sysop_start_postpone_rec.m_rectype == LOG_SYSOP_START_POSTPONE);
  REQUIRE (trid1_sysop_start_postpone_rec.m_lsa != INV_LSA);
  //const LOG_LSA &trid1_sysop_end_last_parent_lsa = trid1_sysop_start_postpone_rec.m_lsa;
  log_rec_vec.push_back (
  {
    trid1, INV_LSA, INV_VPID, LOG_SYSOP_END, INV_RCVINDEX, LOG_SYSOP_END_LOGICAL_RUN_POSTPONE,
    trid1_sysop_start_postpone_rec.m_lsa, FIX_SUCC });

  // trid2
  //
  log_rec_vec.push_back (
  { trid2, INV_LSA, INV_VPID, LOG_SYSOP_ATOMIC_START, INV_RCVINDEX, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC });
  log_rec_vec.push_back (
  { trid2, INV_LSA, { 224, 0 }, LOG_UNDOREDO_DATA, RVFL_PARTSECT_DEALLOC, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC });
  log_rec_vec.push_back (
  { trid2, INV_LSA, { 224, 0 }, LOG_UNDOREDO_DATA, RVFL_FHEAD_DEALLOC, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC });
  log_rec_vec.push_back (
  { trid2, INV_LSA, { 725, 0 }, LOG_UNDOREDO_DATA, RVPGBUF_DEALLOC, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC });
  test_spec.calculate_log_records_offsets (start_lsa);
  log_record_spec_type &trid2_sysop_start_postpone_rec = log_rec_vec[7];
  REQUIRE (trid2_sysop_start_postpone_rec.m_rectype == LOG_SYSOP_START_POSTPONE);
  REQUIRE (trid2_sysop_start_postpone_rec.m_lsa != INV_LSA);
  log_rec_vec.push_back (
  {
    trid2, INV_LSA, INV_VPID, LOG_SYSOP_END, INV_RCVINDEX, LOG_SYSOP_END_LOGICAL_RUN_POSTPONE,
    trid2_sysop_start_postpone_rec.m_lsa, FIX_SUCC });
  log_rec_vec.push_back (
  { trid2, INV_LSA, INV_VPID, LOG_SYSOP_END, INV_RCVINDEX, LOG_SYSOP_END_COMMIT, INV_LSA, FIX_SUCC });

  // trid1
  //
  log_rec_vec.push_back (
  { trid1, INV_LSA, INV_VPID, LOG_SYSOP_END, INV_RCVINDEX, LOG_SYSOP_END_COMMIT, INV_LSA, FIX_SUCC });

  test_spec.calculate_log_records_offsets (start_lsa);

  gl_Test_Spec = &test_spec;

  cublog::atomic_replication_helper atomic_helper;
  test_spec.execute (atomic_helper);
  test_spec.check_after_execution (trid1, atomic_helper);
  test_spec.check_after_execution (trid2, atomic_helper);
}

TEST_CASE ("LOG_SYSOP_ATOMIC_START/LOG_SYSOP_END-LOG_SYSOP_END_LOGICAL_COMPENSATE", "")
{
  // logging snippet:
  // _CL_ LSA = 136|3880  rectype = LOG_SYSOP_ATOMIC_START  sysop_end_type = N_A  sysop_end_last_parent_lsa = -1|-1
  // _WK_ LSA = 136|3912  vpid = 0|4096  rcvindex = RVFL_PARTSECT_DEALLOC
  // _WK_ LSA = 136|3984  vpid = 0|4096  rcvindex = RVFL_FHEAD_DEALLOC
  // _WK_ LSA = 136|4056  vpid = 0|4096  rcvindex = RVPGBUF_DEALLOC
  // _CL_ LSA = 136|4320  rectype = LOG_SYSOP_END  sysop_end_type = LOG_SYSOP_END_LOGICAL_COMPENSATE  sysop_end_last_parent_lsa = 136|2544

  test_spec_type test_spec;

  log_record_spec_vector_type &log_rec_vec = test_spec.m_log_record_vec;
  constexpr TRANID trid = 5;
  constexpr LOG_LSA start_lsa = { 40, 11 };
  log_rec_vec =
  {
    { trid, INV_LSA, INV_VPID, LOG_SYSOP_ATOMIC_START, INV_RCVINDEX, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 60, 0 }, INV_RECTYPE, RVFL_PARTSECT_DEALLOC, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 60, 0 }, INV_RECTYPE, RVFL_FHEAD_DEALLOC, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
    { trid, INV_LSA, { 60, 0 }, INV_RECTYPE, RVPGBUF_DEALLOC, INV_SYSOP_END_TYPE, INV_LSA, FIX_SUCC },
  };
  test_spec.calculate_log_records_offsets (start_lsa);
  const LOG_LSA &sysop_end_last_parent_lsa = log_rec_vec[0].m_lsa;
  log_rec_vec.push_back (
  {
    trid, INV_LSA, INV_VPID, LOG_SYSOP_END, INV_RCVINDEX, LOG_SYSOP_END_LOGICAL_COMPENSATE,
    sysop_end_last_parent_lsa, FIX_SUCC });

  test_spec.calculate_log_records_offsets (start_lsa);

  gl_Test_Spec = &test_spec;

  cublog::atomic_replication_helper atomic_helper;
  test_spec.execute (atomic_helper);
  test_spec.check_after_execution (trid, atomic_helper);
}

// ****************************************************************
// test_spec_type implementation
// ****************************************************************

fake_system_parameters_initialize_type::fake_system_parameters_initialize_type ()
{
  prm_set_bool_value (PRM_ID_ER_LOG_DEBUG, true);
  prm_set_bool_value (PRM_ID_ER_LOG_PTS_ATOMIC_REPL_DEBUG, false);
}

test_spec_type::test_spec_type ()
  : m_log_redo_context { NULL_LSA, OLD_PAGE_IF_IN_BUFFER_OR_IN_TRANSIT, log_reader::fetch_mode::FORCE }
{
  m_thread_p = new THREAD_ENTRY;

  gl_Test_Spec = this;
}

test_spec_type::~test_spec_type ()
{
  for (auto &pair: m_fixed_page_map)
    {
      PAGE_PTR const page_ptr = pair.second;
      REQUIRE ((page_ptr == nullptr
		|| m_fixed_page_ptr_set.find (page_ptr) != m_fixed_page_ptr_set.cend ()));
      if (page_ptr != nullptr)
	{
	  delete [] page_ptr;
	}
    }
  m_fixed_page_map.clear ();
  m_fixed_page_ptr_set.clear ();

  if (m_thread_p != nullptr)
    {
      delete m_thread_p;
      m_thread_p = nullptr;
    }
}

void test_spec_type::calculate_log_records_offsets (LOG_LSA start_lsa)
{
  REQUIRE (!m_log_record_vec.empty ());

  // offset alignment to allow writing just the log_rec_header as needed to be read and interpreted by the log reader
  // calculated according to the test spec starting either:
  //  - from the supplied LSA
  //  - from the last calculated LSA onwards (this is needed for specific scenarios in which
  //    adding a new log record in the spec definition, requires having calculated the lsa (offset) of
  //    a previous log record)

  // first, skip all log records with already calculated lsa's
  //
  LOG_LSA lsa = INV_LSA;
  log_record_spec_vector_type::iterator it = m_log_record_vec.begin ();
  for (; it != m_log_record_vec.end (); ++it)
    {
      log_record_spec_type &log_rec = *it;
      if (!LSA_ISNULL (&log_rec.m_lsa))
	{
	  lsa = log_rec.m_lsa;
	}
      else
	{
	  break;
	}
    }

  // if no log record was previously initialized, initialize first lsa as specified
  //
  if (LSA_ISNULL (&lsa))
    {
      // no valid lsa in the vector
      // calculate starting from the supplied start_lsa
      REQUIRE (!LSA_ISNULL (&start_lsa));
      REQUIRE (start_lsa.offset >= 0);
      lsa = start_lsa;
    }
  else
    {
      // will continue from last encountered lsa
    }

  // all remaining log records must have not been initialized
  // calculate LSA's for all the rest
  //
  for (; it != m_log_record_vec.end (); ++it)
    {
      log_record_spec_type &log_rec = *it;
      REQUIRE (LSA_ISNULL (&log_rec.m_lsa));

      lsa.offset += sizeof (log_rec_header);
      lsa.offset = DB_ALIGN (lsa.offset, DOUBLE_ALIGNMENT);
      log_rec.m_lsa = lsa;
    }
}

void test_spec_type::execute (cublog::atomic_replication_helper &atomic_helper)
{
  for (log_record_spec_type &log_rec : m_log_record_vec)
    {
      REQUIRE (log_rec.m_lsa != INV_LSA);
#ifdef UTEST_LOG_REPL_ATOMIC_TEST_FIX_FAIL
      m_current_log_ptr = &log_rec;
#endif

      if (log_rec.m_rectype == LOG_SYSOP_END)
	{
	  REQUIRE (VPID_EQ (&log_rec.m_vpid, &INV_VPID));
	  REQUIRE (log_rec.m_rcvindex == INV_RCVINDEX);
	  REQUIRE (log_rec.m_sysop_end_type != INV_SYSOP_END_TYPE);
	  //REQUIRE (log_rec.m_sysop_end_last_parent_lsa == INV_LSA);

	  atomic_helper.append_control_log_sysop_end (m_thread_p, log_rec.m_trid, log_rec.m_lsa,
	      log_rec.m_sysop_end_type, log_rec.m_sysop_end_last_parent_lsa );
	}
      else if (cublog::atomrepl_is_control (log_rec.m_rectype))
	{
	  REQUIRE (VPID_EQ (&log_rec.m_vpid, &INV_VPID));
	  REQUIRE (log_rec.m_rcvindex == INV_RCVINDEX);
	  //REQUIRE (log_rec.m_sysop_end_type == INV_SYSOP_END_TYPE);
	  REQUIRE (log_rec.m_sysop_end_last_parent_lsa == INV_LSA);

	  atomic_helper.append_control_log (m_thread_p, log_rec.m_trid, log_rec.m_rectype,
					    log_rec.m_lsa, m_log_redo_context);
	}
      else
	{
	  //REQUIRE (log_rec.m_rectype == INV_RECTYPE);
	  REQUIRE (log_rec.m_sysop_end_type == INV_SYSOP_END_TYPE);
	  REQUIRE (log_rec.m_sysop_end_last_parent_lsa == INV_LSA);

	  atomic_helper.append_log (log_rec.m_trid, log_rec.m_lsa, log_rec.m_rcvindex, log_rec.m_vpid);
	}

#ifdef UTEST_LOG_REPL_ATOMIC_TEST_FIX_FAIL
      if (m_current_log_ptr->m_severe_error_occured)
	{
	  atomic_helper.forcibly_remove_sequence (m_current_log_ptr->m_trid);
	  break;
	}
#endif
    }

#ifdef UTEST_LOG_REPL_ATOMIC_TEST_FIX_FAIL
  m_current_log_ptr = nullptr;
#endif
}

void test_spec_type::check_after_execution (TRANID trid, const cublog::atomic_replication_helper &atomic_helper)
{
  REQUIRE (!atomic_helper.is_part_of_atomic_replication (trid));
  REQUIRE (atomic_helper.all_log_entries_are_control (trid));

#ifdef UTEST_LOG_REPL_ATOMIC_TEST_FIX_FAIL
  log_record_spec_vector_type::const_iterator log_rec_it = m_log_record_vec.cbegin ();
  VPID fix_fail_vpid = INV_VPID;
  for ( ; log_rec_it != m_log_record_vec.cend (); ++log_rec_it)
    {
      const log_record_spec_type &log_rec = *log_rec_it;
      if (log_rec.m_fix_success == FIX_FAIL)
	{
	  fix_fail_vpid = log_rec.m_vpid;
	  break;
	}
    }
  bool severe_error_occured_after_failed_page_fix = false;
  for ( ; log_rec_it != m_log_record_vec.cend (); ++log_rec_it)
    {
      const log_record_spec_type &log_rec = *log_rec_it;
      if (log_rec.m_severe_error_occured)
	{
	  REQUIRE (!VPID_ISNULL (&fix_fail_vpid));
	  REQUIRE (VPID_EQ (&log_rec.m_vpid, &fix_fail_vpid));
	  severe_error_occured_after_failed_page_fix = true;
	  break;
	}
    }
  if (!VPID_ISNULL (&fix_fail_vpid))
    {
      REQUIRE (severe_error_occured_after_failed_page_fix);
    }
#endif
}

PAGE_PTR test_spec_type::alloc_page (const VPID &vpid)
{
  PAGE_PTR const page_ptr = new char[256];
  // some debug message; might help
  // cppcheck-suppress [wrongPrintfScanfArgNum]
  snprintf (page_ptr, 256, "page with vpid=%d|%d", VPID_AS_ARGS (&vpid));

  return page_ptr;
}

PAGE_PTR test_spec_type::fix_page (const VPID &vpid)
{
  REQUIRE (!VPID_ISNULL (&vpid));
  //REQUIRE (m_fixed_page_map.find (vpid) == m_fixed_page_map.cend ());
#ifdef UTEST_LOG_REPL_ATOMIC_TEST_FIX_FAIL
  REQUIRE (m_current_log_ptr != nullptr);
#endif

  const vpid_page_ptr_map::iterator find_it = m_fixed_page_map.find (vpid);
  if (find_it == m_fixed_page_map.cend ())
    {
#ifdef UTEST_LOG_REPL_ATOMIC_TEST_FIX_FAIL
      if (m_current_log_ptr->m_fix_success == FIX_SUCC)
	{
#endif
	  // simulare successful fixing
	  PAGE_PTR const page_ptr = alloc_page (vpid);
	  m_fixed_page_map.insert (std::make_pair (vpid, page_ptr));
	  m_fixed_page_ptr_set.insert (page_ptr);
	  return page_ptr;
#ifdef UTEST_LOG_REPL_ATOMIC_TEST_FIX_FAIL
	}
      else
	{
	  // insert an empty entry
	  m_fixed_page_map.insert (std::make_pair (vpid, nullptr));
	  return nullptr;
	}
#endif
    }
  else
    {
#ifdef UTEST_LOG_REPL_ATOMIC_TEST_FIX_FAIL
      // if the pgptr is null; it means that the logic is trying a second time
      // to fix a page that was specified to fail to fix a previous time
      if (find_it->second == nullptr && m_current_log_ptr->m_fix_success == FIX_SUCC)
	{
	  PAGE_PTR const page_ptr = alloc_page (vpid);
	  find_it->second = page_ptr;
	  m_fixed_page_ptr_set.insert (page_ptr);
	}
#endif

      REQUIRE (find_it->second != nullptr);
      return find_it->second;
    }
}

void test_spec_type::unfix_page (PAGE_PTR page_ptr)
{
  REQUIRE (page_ptr != nullptr);
  REQUIRE (m_fixed_page_ptr_set.find (page_ptr) != m_fixed_page_ptr_set.cend ());

  // nothing to do actually because we chose not to implement reference counting
  // in the unit test as well; therefore, pages are "fixed" when requested and only
  // unfixed when the test spec is destroyed
}

void test_spec_type::check_error (int severity, int err_id)
{
#ifdef UTEST_LOG_REPL_ATOMIC_TEST_FIX_FAIL
  REQUIRE (m_current_log_ptr != nullptr);

  REQUIRE (ER_FATAL_ERROR_SEVERITY == severity);
  REQUIRE ((m_current_log_ptr->m_fix_success && err_id == ER_ATOMIC_REPL_ERROR));

  if (ER_FATAL_ERROR_SEVERITY == severity)
    {
      m_current_log_ptr->m_severe_error_occured = true;
    }
#else
  REQUIRE (false);
#endif
}

// ****************************************************************
// CUBRID stuff mocking
// ****************************************************************

PGLENGTH db_Log_page_size = IO_DEFAULT_PAGE_SIZE;

void
_er_log_debug (const char */*file_name*/, const int /*line_no*/, const char */*fmt*/, ...)
{
  //va_list ap;
  //va_start (ap, fmt);
  //vprintf(fmt, ap);
  //va_end (ap);
}

struct rvfun RV_fun[] =
{
  {
    RV_NOT_DEFINED,
    "RV_NOT_DEFINED",
    nullptr,
    nullptr,
    nullptr,
    nullptr
  },
};

namespace cubthread
{
  entry &
  get_entry ()
  {
    assert (gl_Test_Spec != nullptr);
    return *gl_Test_Spec->m_thread_p;
  }
}

PAGE_PTR
#if !defined(NDEBUG)
pgbuf_fix_debug
#else /* NDEBUG */
pgbuf_fix_release
#endif /* NDEBUG */
(THREAD_ENTRY * /*thread_p*/, const VPID *vpid, PAGE_FETCH_MODE /*fetch_mode*/
 , PGBUF_LATCH_MODE /*request_mode*/, PGBUF_LATCH_CONDITION /*condition*/
#if !defined(NDEBUG)
 , const char */*caller_file*/, int /*caller_line*/
#endif /* NDEBUG */
)
{
  assert (gl_Test_Spec != nullptr);

  PAGE_PTR const pgptr = gl_Test_Spec->fix_page (*vpid);
  // some pages are specified to fail to fix
  //REQUIRE (pgptr != nullptr);

  return pgptr;
}

void
#if !defined(NDEBUG)
pgbuf_unfix_debug
#else /* NDEBUG */
pgbuf_unfix
#endif /* NDEBUG */
(THREAD_ENTRY * /*thread_p*/, PAGE_PTR pgptr
#if !defined(NDEBUG)
 , const char */*caller_file*/, int /*caller_line*/
#endif /* NDEBUG */
)
{
  assert (gl_Test_Spec != nullptr);

  gl_Test_Spec->unfix_page (pgptr);
}

int
#if !defined(NDEBUG)
pgbuf_ordered_fix_debug
#else /* NDEBUG */
pgbuf_ordered_fix_release
#endif /* NDEBUG */
(THREAD_ENTRY * /*thread_p*/, const VPID *req_vpid, PAGE_FETCH_MODE /*fetch_mode*/,
 const PGBUF_LATCH_MODE /*request_mode*/, PGBUF_WATCHER *req_watcher
#if !defined(NDEBUG)
 , const char */*caller_file*/, int /*caller_line*/
#endif /* NDEBUG */
)
{
  assert (gl_Test_Spec != nullptr);
  assert (req_watcher != nullptr);
  assert (req_watcher->pgptr == nullptr);

  req_watcher->pgptr = gl_Test_Spec->fix_page (*req_vpid);
  // some pages are specified to fail to fix
  //REQUIRE (req_watcher->pgptr != nullptr);

  return NO_ERROR;
}

void
#if !defined(NDEBUG)
pgbuf_ordered_unfix_debug
#else /* NDEBUG */
pgbuf_ordered_unfix
#endif /* NDEBUG */
(THREAD_ENTRY * /*thread_p*/, PGBUF_WATCHER *watcher_object
#if !defined(NDEBUG)
 , const char */*caller_file*/, int /*caller_line*/
#endif /* NDEBUG */
)
{
  assert (gl_Test_Spec != nullptr);
  assert (watcher_object != nullptr);

  gl_Test_Spec->unfix_page (watcher_object->pgptr);
}

int
logpb_fetch_page (THREAD_ENTRY * /*thread_p*/, const LOG_LSA * /*req_lsa*/, LOG_CS_ACCESS_MODE /*access_mode*/,
		  LOG_PAGE *log_pgptr)
{
  assert (gl_Test_Spec != nullptr);
  REQUIRE (log_pgptr != nullptr);

  // TODO: optimization: only initialize upon first call of an req_lsa

  // map in the supplied log page just enough to "fool" one function in the apply chain - namely
  // "atomic_log_entry::apply_log_redo" to think it has "nop" to do;
  // this is achieved by means of a dummy log record type - LOG_DUMMY_UNIT_TESTING -
  // with debug only compilation
  log_rec_header log_rec_hdr;
  log_rec_hdr.prev_tranlsa = INV_LSA;
  log_rec_hdr.back_lsa = INV_LSA;
  log_rec_hdr.forw_lsa = INV_LSA;
  log_rec_hdr.type = LOG_DUMMY_UNIT_TESTING;
  log_record_spec_vector_type &log_rec_vec = gl_Test_Spec->m_log_record_vec;
  char *log_pgptr_area = (char *)log_pgptr->area;
  for (log_record_spec_type &log_rec : log_rec_vec)
    {
      log_rec_hdr.trid = log_rec.m_trid;
      memcpy (log_pgptr_area + log_rec.m_lsa.offset, &log_rec_hdr, sizeof (log_rec_hdr));
    }

  return NO_ERROR;
}

log_rv_redo_context::log_rv_redo_context (const log_lsa &end_redo_lsa, PAGE_FETCH_MODE page_fetch_mode,
    log_reader::fetch_mode reader_fetch_page_mode)
  : m_end_redo_lsa { end_redo_lsa }
  , m_page_fetch_mode { page_fetch_mode }
  , m_reader_fetch_page_mode { reader_fetch_page_mode }
{
}

log_rv_redo_context::log_rv_redo_context (const log_rv_redo_context &that)
  : log_rv_redo_context { that.m_end_redo_lsa, that.m_page_fetch_mode, that.m_reader_fetch_page_mode }
{
}

log_rv_redo_context::~log_rv_redo_context ()
{
}

void
er_set (int severity, const char */*file_name*/, const int /*line_no*/, int err_id, int /*num_args*/, ...)
{
  gl_Test_Spec->check_error (severity, err_id);
}

// ****************************************************************
// CUBRID stuff; required by linker:
//  - whatever is not (should not be) touched at all is asserted
//  - some of it actually called with benign/no effect
// ****************************************************************

const char *
rv_rcvindex_string (LOG_RCVINDEX /*rcvindex*/)
{
  assert_release (false);
  return nullptr;
}

const char *
log_sysop_end_type_string (LOG_SYSOP_END_TYPE /*end_type*/)
{
  assert_release (false);
  return nullptr;
}

const char *
log_to_string (LOG_RECTYPE /*type*/)
{
  assert_release (false);
  return nullptr;
}

perfmon_counter_timer_tracker::perfmon_counter_timer_tracker (PERF_STAT_ID a_stat_id)
  : m_stat_id (a_stat_id)
{
  assert_release (false);
}

perfmon_counter_timer_raii_tracker::perfmon_counter_timer_raii_tracker (PERF_STAT_ID a_stat_id)
  : m_tracker (a_stat_id)
{
  assert_release (false);
}

perfmon_counter_timer_raii_tracker::~perfmon_counter_timer_raii_tracker ()
{
  assert_release (false);
}

void
logpb_fatal_error (THREAD_ENTRY */*thread_p*/, bool /*log_exit*/, const char */*file_name*/,
		   const int /*lineno*/, const char */*fmt*/, ...)
{
  assert_release (false);
}

HFID *pgbuf_ordered_null_hfid = nullptr;

#if !defined(NDEBUG)
// NOTE: same implementation as in page_buffer.c module as it has no side effects
void
pgbuf_watcher_init_debug (PGBUF_WATCHER *watcher, const char *caller_file,
			  const int caller_line, bool add)
{
  char *p;

  p = (char *) caller_file + strlen (caller_file);
  while (p)
    {
      if (p == caller_file)
	{
	  break;
	}

      if (*p == '/' || *p == '\\')
	{
	  p++;
	  break;
	}

      p--;
    }

  if (add)
    {
      char prev_init[256];
      strncpy (prev_init, watcher->init_at, sizeof (watcher->init_at) - 1);
      prev_init[sizeof (prev_init) - 1] = '\0';
      snprintf_dots_truncate (watcher->init_at, sizeof (watcher->init_at) - 1, "%s:%d %s", p, caller_line, prev_init);
    }
  else
    {
      snprintf (watcher->init_at, sizeof (watcher->init_at) - 1, "%s:%d", p, caller_line);
    }
}
#endif

LOG_LSA *
pgbuf_get_lsa (PAGE_PTR /*pgptr*/)
{
  assert_release (false);
  return nullptr;
}

const LOG_LSA *
pgbuf_set_lsa (THREAD_ENTRY * /*thread_p*/, PAGE_PTR /*pgptr*/, const LOG_LSA * /*lsa_ptr*/)
{
  assert_release (false);
  return nullptr;
}

LOG_GLOBAL log_Gl;

namespace cublog
{
  EXPAND_PACKABLE_OBJECT_EMPTY_DEF (meta);

  EXPAND_PACKABLE_OBJECT_EMPTY_DEF (checkpoint_info);
}

mvcc_active_tran::mvcc_active_tran () = default;
mvcc_active_tran::~mvcc_active_tran () = default;

mvcc_trans_status::mvcc_trans_status () = default;
mvcc_trans_status::~mvcc_trans_status () = default;

mvcctable::mvcctable () = default;
mvcctable::~mvcctable () = default;

log_append_info::log_append_info () = default;

log_prior_lsa_info::log_prior_lsa_info () = default;

namespace cublog
{
  prior_recver::~prior_recver ()
  {
    assert_release (!m_thread.joinable ());
  }

  prior_sender::~prior_sender ()
  {
    assert_release (false);
  }
}

log_global::log_global ()
{
}

log_global::~log_global ()
{
}

int
log_rv_get_unzip_log_data (THREAD_ENTRY * /*thread_p*/, int /*length*/, log_reader & /*log_pgptr_reader*/,
			   LOG_ZIP * /*unzip_ptr*/, bool & /*is_zip*/)
{
  assert_release (false);
  return ER_FAILED;
}

int
log_rv_get_unzip_and_diff_redo_log_data (THREAD_ENTRY * /*thread_p*/, log_reader & /*log_pgptr_reader*/,
    LOG_RCV * /*rcv*/, int /*undo_length*/, const char */*undo_data*/,
    LOG_ZIP & /*redo_unzip*/)
{
  assert_release (false);
  return ER_FAILED;
}

bool
log_rv_check_redo_is_needed (const PAGE_PTR & /*pgptr*/, const LOG_LSA & /*rcv_lsa*/,
			     const LOG_LSA & /*end_redo_lsa*/)
{
  assert_release (false);
  return false;
}
