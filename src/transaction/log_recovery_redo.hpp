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

#ifndef LOC_RECOVERY_REDO_LOG_REC_HPP
#define LOC_RECOVERY_REDO_LOG_REC_HPP

#include "log_reader.hpp"
#include "log_record.hpp"
#include "log_recovery.h"
#include "page_buffer.h"
#include "scope_exit.hpp"
#include "system_parameter.h"
#include "type_helper.hpp"

/*
 * helper functions to assist with log recovery redo
 */

/*
 * template declarations
 */

/* recovery data out of a log record
 */
template <typename T>
const LOG_DATA &log_rv_get_log_rec_data (const T &log_rec);

/*
 */
template <typename T>
MVCCID log_rv_get_log_rec_mvccid (const T &log_rec);

/*
 */
template <typename T>
VPID log_rv_get_log_rec_vpid (const T &log_rec);

/*
 */
template <typename T>
int log_rv_get_log_rec_redo_length (const T &log_rec);

/*
 */
template <typename T>
int log_rv_get_log_rec_offset (const T &log_rec);

/*
 */
template <typename T>
rvfun::fun_t log_rv_get_fun (const T &, LOG_RCVINDEX rcvindex);

/*
 * template implementations
 */

template <typename T>
const LOG_DATA &log_rv_get_log_rec_data (const T &log_rec)
{
  static_assert (sizeof (T) == 0, "should not be called");
  static constexpr log_data LOG_DATA_INITIALIZER =
  {
    /*.rcvindex =*/ RV_NOT_DEFINED,
    /*.pageid =*/ NULL_PAGEID,
    /*.offset =*/ -1,
    /*.volid =*/ NULL_VOLID
  };
  static const LOG_DATA dummy = LOG_DATA_INITIALIZER;
  return dummy;
}

template <>
inline const LOG_DATA &log_rv_get_log_rec_data<LOG_REC_MVCC_UNDOREDO> (const LOG_REC_MVCC_UNDOREDO &log_rec)
{
  return log_rec.undoredo.data;
}

template <>
inline const LOG_DATA &log_rv_get_log_rec_data<LOG_REC_UNDOREDO> (const LOG_REC_UNDOREDO &log_rec)
{
  return log_rec.data;
}

template <>
inline const LOG_DATA &log_rv_get_log_rec_data<LOG_REC_MVCC_REDO> (const LOG_REC_MVCC_REDO &log_rec)
{
  return log_rec.redo.data;
}

template <>
inline const LOG_DATA &log_rv_get_log_rec_data<LOG_REC_REDO> (const LOG_REC_REDO &log_rec)
{
  return log_rec.data;
}

template <>
inline const LOG_DATA &log_rv_get_log_rec_data<LOG_REC_RUN_POSTPONE> (const LOG_REC_RUN_POSTPONE &log_rec)
{
  return log_rec.data;
}

template <>
inline const LOG_DATA &log_rv_get_log_rec_data<LOG_REC_COMPENSATE> (const LOG_REC_COMPENSATE &log_rec)
{
  return log_rec.data;
}

template <typename T>
MVCCID log_rv_get_log_rec_mvccid (const T &)
{
  static_assert (sizeof (T) == 0, "should not be called");
  return MVCCID_NULL;
}

template <>
inline MVCCID log_rv_get_log_rec_mvccid<LOG_REC_MVCC_UNDOREDO> (const LOG_REC_MVCC_UNDOREDO &log_rec)
{
  return log_rec.mvccid;
}

template <>
inline MVCCID log_rv_get_log_rec_mvccid<LOG_REC_UNDOREDO> (const LOG_REC_UNDOREDO &log_rec)
{
  return MVCCID_NULL;
}

template <>
inline MVCCID log_rv_get_log_rec_mvccid<LOG_REC_MVCC_REDO> (const LOG_REC_MVCC_REDO &log_rec)
{
  return log_rec.mvccid;
}

template <>
inline MVCCID log_rv_get_log_rec_mvccid<LOG_REC_REDO> (const LOG_REC_REDO &log_rec)
{
  return MVCCID_NULL;
}

template <>
inline MVCCID log_rv_get_log_rec_mvccid<LOG_REC_RUN_POSTPONE> (const LOG_REC_RUN_POSTPONE &log_rec)
{
  return MVCCID_NULL;
}

template <>
inline MVCCID log_rv_get_log_rec_mvccid<LOG_REC_COMPENSATE> (const LOG_REC_COMPENSATE &log_rec)
{
  return MVCCID_NULL;
}

template <typename T>
VPID log_rv_get_log_rec_vpid (const T &log_rec)
{
  static_assert (sizeof (T) == 0, "should not be called");
  return VPID_INITIALIZER;
}

template <>
inline VPID log_rv_get_log_rec_vpid<LOG_REC_MVCC_UNDOREDO> (const LOG_REC_MVCC_UNDOREDO &log_rec)
{
  return
  {
    log_rec.undoredo.data.pageid,
    log_rec.undoredo.data.volid
  };
}

template <>
inline VPID log_rv_get_log_rec_vpid<LOG_REC_UNDOREDO> (const LOG_REC_UNDOREDO &log_rec)
{
  return
  {
    log_rec.data.pageid,
    log_rec.data.volid
  };
}

template <>
inline VPID log_rv_get_log_rec_vpid<LOG_REC_MVCC_REDO> (const LOG_REC_MVCC_REDO &log_rec)
{
  return
  {
    log_rec.redo.data.pageid,
    log_rec.redo.data.volid
  };
}

template <>
inline VPID log_rv_get_log_rec_vpid<LOG_REC_REDO> (const LOG_REC_REDO &log_rec)
{
  return
  {
    log_rec.data.pageid,
    log_rec.data.volid
  };
}

template <>
inline VPID log_rv_get_log_rec_vpid<LOG_REC_RUN_POSTPONE> (const LOG_REC_RUN_POSTPONE &log_rec)
{
  return
  {
    log_rec.data.pageid,
    log_rec.data.volid
  };
}

template <>
inline VPID log_rv_get_log_rec_vpid<LOG_REC_COMPENSATE> (const LOG_REC_COMPENSATE &log_rec)
{
  return
  {
    log_rec.data.pageid,
    log_rec.data.volid
  };
}

template <typename T>
int log_rv_get_log_rec_redo_length (const T &log_rec)
{
  static_assert (sizeof (T) == 0, "should not be called");
  return -1;
}

template <>
inline int log_rv_get_log_rec_redo_length<LOG_REC_MVCC_UNDOREDO> (const LOG_REC_MVCC_UNDOREDO &log_rec)
{
  return log_rec.undoredo.rlength;
}

template <>
inline int log_rv_get_log_rec_redo_length<LOG_REC_UNDOREDO> (const LOG_REC_UNDOREDO &log_rec)
{
  return log_rec.rlength;
}

template <>
inline int log_rv_get_log_rec_redo_length<LOG_REC_MVCC_REDO> (const LOG_REC_MVCC_REDO &log_rec)
{
  return log_rec.redo.length;
}

template <>
inline int log_rv_get_log_rec_redo_length<LOG_REC_REDO> (const LOG_REC_REDO &log_rec)
{
  return log_rec.length;
}

template <>
inline int log_rv_get_log_rec_redo_length<LOG_REC_RUN_POSTPONE> (const LOG_REC_RUN_POSTPONE &log_rec)
{
  return log_rec.length;
}

template <>
inline int log_rv_get_log_rec_redo_length<LOG_REC_COMPENSATE> (const LOG_REC_COMPENSATE &log_rec)
{
  return log_rec.length;
}

template <typename T>
int log_rv_get_log_rec_offset (const T &log_rec)
{
  static_assert (sizeof (T) == 0, "should not be called");
  return -1;
}

template <>
inline int log_rv_get_log_rec_offset<LOG_REC_MVCC_UNDOREDO> (const LOG_REC_MVCC_UNDOREDO &log_rec)
{
  return log_rec.undoredo.data.offset;
}

template <>
inline int log_rv_get_log_rec_offset<LOG_REC_UNDOREDO> (const LOG_REC_UNDOREDO &log_rec)
{
  return log_rec.data.offset;
}

template <>
inline int log_rv_get_log_rec_offset<LOG_REC_MVCC_REDO> (const LOG_REC_MVCC_REDO &log_rec)
{
  return log_rec.redo.data.offset;
}

template <>
inline int log_rv_get_log_rec_offset<LOG_REC_REDO> (const LOG_REC_REDO &log_rec)
{
  return log_rec.data.offset;
}

template <>
inline int log_rv_get_log_rec_offset<LOG_REC_RUN_POSTPONE> (const LOG_REC_RUN_POSTPONE &log_rec)
{
  return log_rec.data.offset;
}

template <>
inline int log_rv_get_log_rec_offset<LOG_REC_COMPENSATE> (const LOG_REC_COMPENSATE &log_rec)
{
  return log_rec.data.offset;
}

template <typename T>
rvfun::fun_t log_rv_get_fun (const T &, LOG_RCVINDEX rcvindex)
{
  static_assert (sizeof (T) == 0, "should not be called");
  return nullptr;
}

template <>
inline rvfun::fun_t log_rv_get_fun<LOG_REC_MVCC_UNDOREDO> (const LOG_REC_MVCC_UNDOREDO &, LOG_RCVINDEX rcvindex)
{
  return RV_fun[rcvindex].redofun;
}

template <>
inline rvfun::fun_t log_rv_get_fun<LOG_REC_UNDOREDO> (const LOG_REC_UNDOREDO &, LOG_RCVINDEX rcvindex)
{
  return RV_fun[rcvindex].redofun;
}

template <>
inline rvfun::fun_t log_rv_get_fun<LOG_REC_MVCC_REDO> (const LOG_REC_MVCC_REDO &, LOG_RCVINDEX rcvindex)
{
  return RV_fun[rcvindex].redofun;
}

template <>
inline rvfun::fun_t log_rv_get_fun<LOG_REC_REDO> (const LOG_REC_REDO &, LOG_RCVINDEX rcvindex)
{
  return RV_fun[rcvindex].redofun;
}

template <>
inline rvfun::fun_t log_rv_get_fun<LOG_REC_RUN_POSTPONE> (const LOG_REC_RUN_POSTPONE &, LOG_RCVINDEX rcvindex)
{
  return RV_fun[rcvindex].redofun;
}

template <>
inline rvfun::fun_t log_rv_get_fun<LOG_REC_COMPENSATE> (const LOG_REC_COMPENSATE &, LOG_RCVINDEX rcvindex)
{
  // yes, undo
  return RV_fun[rcvindex].undofun;
}

#if !defined(NDEBUG)
DBG_REGISTER_PARSE_TYPE_NAME (LOG_REC_MVCC_UNDOREDO)
DBG_REGISTER_PARSE_TYPE_NAME (LOG_REC_UNDOREDO)
DBG_REGISTER_PARSE_TYPE_NAME (LOG_REC_MVCC_REDO)
DBG_REGISTER_PARSE_TYPE_NAME (LOG_REC_REDO)
DBG_REGISTER_PARSE_TYPE_NAME (LOG_REC_RUN_POSTPONE)
DBG_REGISTER_PARSE_TYPE_NAME (LOG_REC_COMPENSATE)
#endif

/* log_rv_redo_record_debug_logging - utility function which prints debug information
 *                        templated in order to be able to specifically print out user readable name
 *                        of the log record structure
 */
template <typename T>
void log_rv_redo_record_debug_logging (const log_lsa &rcv_lsa, LOG_RCVINDEX rcvindex, const vpid &rcv_vpid,
				       const log_rcv &rcv)
{
#if !defined(NDEBUG)
  if (prm_get_bool_value (PRM_ID_LOG_TRACE_DEBUG))
    {
      constexpr const char *const log_rec_name = dbg_parse_type_name<T> ();
      fprintf (stdout,
	       "TRACE REDOING[%s]: LSA = %lld|%d, Rv_index = %s,\n"
	       "      volid = %d, pageid = %d, offset = %d,\n", log_rec_name, LSA_AS_ARGS (&rcv_lsa),
	       rv_rcvindex_string (rcvindex), rcv_vpid.volid, rcv_vpid.pageid, rcv.offset);
      if (rcv.pgptr != nullptr)
	{
	  const log_lsa *const rcv_page_lsaptr = pgbuf_get_lsa (rcv.pgptr);
	  assert (rcv_page_lsaptr != nullptr);
	  fprintf (stdout, "      page_lsa = %lld|%d\n", LSA_AS_ARGS (rcv_page_lsaptr));
	}
      else
	{
	  fprintf (stdout, "      page_lsa = %lld|%d\n", -1LL, -1);
	}
      fflush (stdout);
    }
#endif /* !NDEBUG */
}

/*
 * log_rv_get_log_rec_redo_data - GET UNZIPPED [and DIFFED, if needed] REDO LOG DATA FROM LOG
 *
 * return: error code
 *
 *   thread_p(in):
 *   log_pgptr_reader(in/out): log reader
 *   log_rec(in): log record structure with info about the location and size of the data in the log page
 *   rcv(in/out): Recovery structure for recovery function
 *   log_rtype(in): log record type needed to check if diff information is needed or should be skipped
 *   undo_unzip_support(out): extracted undo data support structure (set as a side effect)
 *   redo_unzip_support(out): extracted redo data support structure (set as a side effect); required to
 *                    be passed by address because it also functions as an internal working buffer
 */
template <typename T>
int log_rv_get_log_rec_redo_data (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader, const T &log_rec,
				  log_rcv &rcv, log_rectype log_rtype, struct log_zip &undo_unzip_support, struct log_zip &redo_unzip_support)
{
  static_assert (sizeof (T) == 0, "should not be called");
  return -1;
}

template <>
inline int log_rv_get_log_rec_redo_data<LOG_REC_UNDOREDO> (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
    const LOG_REC_UNDOREDO &log_rec, log_rcv &rcv, log_rectype log_rtype, struct log_zip &undo_unzip_support,
    struct log_zip &redo_unzip_support)
{
  /* current log reader position is aligned at the undo data, redo data follows (aligned) the undo data
   */
  const bool need_diff_with_undo = (log_rtype == LOG_DIFF_UNDOREDO_DATA || log_rtype == LOG_MVCC_DIFF_UNDOREDO_DATA);
  if (need_diff_with_undo)
    {
      /* for the diff log records, undo and redo data must be read, the diff be applied between the undo and the redo
       * to reconstruct the actual redo data
       */
      bool dummy_is_zip = false;
      const int err_undo_unzip =
	      log_rv_get_unzip_log_data (thread_p, log_rec.ulength, log_pgptr_reader, &undo_unzip_support,
					 dummy_is_zip);
      if (err_undo_unzip != NO_ERROR)
	{
	  log_Gl.unique_stats_table.curr_rcv_rec_lsa.set_null ();
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_get_log_rec_redo_data");
	  return err_undo_unzip;
	}
      log_pgptr_reader.align ();
      return log_rv_get_unzip_and_diff_redo_log_data (thread_p, log_pgptr_reader, &rcv, undo_unzip_support.data_length,
	     undo_unzip_support.log_data, redo_unzip_support);
    }
  else
    {
      /* for the non-diff log records, it is enough to skip undo data and read the redo data
       */
      const int temp_length = GET_ZIP_LEN (log_rec.ulength);
      const int err_skip = log_pgptr_reader.skip (temp_length);
      if (err_skip != NO_ERROR)
	{
	  log_Gl.unique_stats_table.curr_rcv_rec_lsa.set_null ();
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_get_log_rec_redo_data");
	  return ER_FAILED;
	}
      log_pgptr_reader.align ();
      return log_rv_get_unzip_and_diff_redo_log_data (thread_p, log_pgptr_reader, &rcv, 0, nullptr, redo_unzip_support);
    }
}

template <>
inline int log_rv_get_log_rec_redo_data<LOG_REC_MVCC_UNDOREDO> (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
    const LOG_REC_MVCC_UNDOREDO &log_rec, log_rcv &rcv, log_rectype log_rtype, struct log_zip &undo_unzip_support,
    struct log_zip &redo_unzip_support)
{
  return log_rv_get_log_rec_redo_data<LOG_REC_UNDOREDO> (thread_p, log_pgptr_reader, log_rec.undoredo,
	 rcv, log_rtype, undo_unzip_support, redo_unzip_support);
}

template <>
inline int log_rv_get_log_rec_redo_data<LOG_REC_MVCC_REDO> (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
    const LOG_REC_MVCC_REDO &log_rec, log_rcv &rcv, log_rectype log_rtype, struct log_zip &undo_unzip_support,
    struct log_zip &redo_unzip_support)
{
  return log_rv_get_unzip_and_diff_redo_log_data (thread_p, log_pgptr_reader, &rcv, 0, nullptr, redo_unzip_support);
}

template <>
inline int log_rv_get_log_rec_redo_data<LOG_REC_REDO> (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
    const LOG_REC_REDO &log_rec, log_rcv &rcv, log_rectype log_rtype, struct log_zip &undo_unzip_support,
    struct log_zip &redo_unzip_support)
{
  return log_rv_get_unzip_and_diff_redo_log_data (thread_p, log_pgptr_reader, &rcv, 0, nullptr, redo_unzip_support);
}

template <>
inline int log_rv_get_log_rec_redo_data<LOG_REC_RUN_POSTPONE> (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
    const LOG_REC_RUN_POSTPONE &log_rec, log_rcv &rcv, log_rectype log_rtype, struct log_zip &undo_unzip_support,
    struct log_zip &redo_unzip_support)
{
  return log_rv_get_unzip_and_diff_redo_log_data (thread_p, log_pgptr_reader, &rcv, 0, nullptr, redo_unzip_support);
}

template <>
inline int log_rv_get_log_rec_redo_data<LOG_REC_COMPENSATE> (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader,
    const LOG_REC_COMPENSATE &log_rec, log_rcv &rcv, log_rectype log_rtype, struct log_zip &undo_unzip_support,
    struct log_zip &redo_unzip_support)
{
  return log_rv_get_unzip_and_diff_redo_log_data (thread_p, log_pgptr_reader, &rcv, 0, nullptr, redo_unzip_support);
}

class vpid_lsa_consistency_check
{
  public:
    vpid_lsa_consistency_check() = default;
    ~vpid_lsa_consistency_check() = default;

    vpid_lsa_consistency_check (const vpid_lsa_consistency_check &) = delete;
    vpid_lsa_consistency_check (vpid_lsa_consistency_check &&) = delete;

    vpid_lsa_consistency_check &operator= (const vpid_lsa_consistency_check &) = delete;
    vpid_lsa_consistency_check &operator= (vpid_lsa_consistency_check &&) = delete;

    void check (const struct vpid &a_vpid, const struct log_lsa &a_log_lsa);
    void cleanup();

  private:
    using vpid_key_t = std::pair<short, int32_t>;
    using vpid_log_lsa_map_t = std::map<vpid_key_t, struct log_lsa>;

#if !defined(NDEBUG)
    std::mutex mtx;
    vpid_log_lsa_map_t consistency_check_map;
#endif
};

#if !defined(NDEBUG)
extern vpid_lsa_consistency_check log_Gl_recovery_redo_consistency_check;
#endif

template <typename T>
void log_rv_redo_record_sync (THREAD_ENTRY *thread_p, log_reader &log_pgptr_reader, const T &log_rec,
			      const VPID &rcv_vpid, const log_lsa &rcv_lsa, const LOG_LSA *end_redo_lsa, LOG_RECTYPE log_rtype,
			      LOG_ZIP &undo_unzip_support, LOG_ZIP &redo_unzip_support)
{
#if !defined(NDEBUG)
  // bit of debug code to ensure that, should this code be executed asynchronously, within the same page,
  // the lsa is ever-increasing, thus, not altering the order in which it has been added to the log in the first place
  log_Gl_recovery_redo_consistency_check.check (rcv_vpid, rcv_lsa);
#endif

  const LOG_DATA &log_data = log_rv_get_log_rec_data<T> (log_rec);

  LOG_RCV rcv;
  if (!log_rv_fix_page_and_check_redo_is_needed (thread_p, rcv_vpid, rcv, log_data.rcvindex, rcv_lsa, end_redo_lsa))
    {
      /* nothing else needs to be done, see explanation in function */
      assert (rcv.pgptr == nullptr);
      return;
    }
  // at this point, pgptr can be null or not

  /* will take care of unfixing the page, will be correctly de-allocated as it is the same
   * storage class as 'rcv' and allocated on the stack after 'rcv' */
  scope_exit <std::function<void (void)>> unfix_rcv_pgptr (
      // could have used pgbuf_unfix_and_init if it were a function
      [&thread_p, &rcv] ()
  {
    if (rcv.pgptr != nullptr)
      {
	pgbuf_unfix (thread_p, rcv.pgptr);
	rcv.pgptr = nullptr;
      }
  });

  rcv.length = log_rv_get_log_rec_redo_length<T> (log_rec);
  rcv.mvcc_id = log_rv_get_log_rec_mvccid<T> (log_rec);
  rcv.offset = log_rv_get_log_rec_offset<T> (log_rec);

  log_rv_redo_record_debug_logging<T> (rcv_lsa, log_data.rcvindex, rcv_vpid, rcv);

  const auto err_redo_data = log_rv_get_log_rec_redo_data<T> (thread_p, log_pgptr_reader, log_rec, rcv, log_rtype,
			     undo_unzip_support, redo_unzip_support);
  if (err_redo_data != NO_ERROR)
    {
      logpb_fatal_error (thread_p, true, ARG_FILE_LINE, "log_rv_get_log_rec_redo_data");
      // rcv pgptr will be automatically unfixed
      return;
    }

  rvfun::fun_t redofunc = log_rv_get_fun<T> (log_rec, log_data.rcvindex);
  if (redofunc != nullptr)
    {
      const int err_func = redofunc (thread_p, &rcv);
      if (err_func != NO_ERROR)
	{
	  logpb_fatal_error (thread_p, true, ARG_FILE_LINE,
			     "log_rv_redo_record_sync: Error applying redo record at log_lsa=(%lld, %d), "
			     "rcv = {mvccid=%llu, vpid=(%d, %d), offset = %d, data_length = %d}",
			     (long long int) rcv_lsa.pageid, (int) rcv_lsa.offset,
			     (long long int) rcv.mvcc_id, (int) rcv_vpid.pageid, (int) rcv_vpid.volid,
			     (int) rcv.offset, (int) rcv.length);
	}
    }
  else
    {
      er_log_debug (ARG_FILE_LINE,
		    "log_rv_redo_record_sync: WARNING.. There is not a"
		    " REDO (or, possibly, UNDO) function to execute. May produce recovery problems.");
    }

  if (rcv.pgptr != nullptr)
    {
      pgbuf_set_lsa (thread_p, rcv.pgptr, &rcv_lsa);
      // rcv pgptr will be automatically unfixed at the end of the parent scope
    }
}

#endif // LOC_RECOVERY_REDO_LOG_REC_HPP
