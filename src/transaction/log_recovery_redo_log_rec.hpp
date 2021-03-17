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
#include "recovery.h"
#include "storage_common.h"

/* helper functions to assist with log recovery redo
 */

/*
 * template declarations
 */

/* recovery data out of a log record
 */
template <typename T>
inline const LOG_DATA &log_rv_get_log_rec_data (const T &log_rec);

/*
 */
template <typename T>
inline MVCCID log_rv_get_log_rec_mvccid (const T &log_rec);

/*
 */
template <typename T>
inline VPID log_rv_get_log_rec_vpid (const T &log_rec);

/*
 */
template <typename T>
inline int log_rv_get_log_rec_redo_length (const T &log_rec);

/*
 */
template <typename T>
inline int log_rv_get_log_rec_offset (const T &log_rec);

/*
 */
template <typename T>
inline rvfun::fun_t log_rv_get_fun (const T &, LOG_RCVINDEX rcvindex);

/*
 * template implementations
 */

template <typename T>
inline const LOG_DATA &log_rv_get_log_rec_data (const T &log_rec)
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
inline MVCCID log_rv_get_log_rec_mvccid (const T &)
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
inline VPID log_rv_get_log_rec_vpid (const T &log_rec)
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
inline int log_rv_get_log_rec_redo_length (const T &log_rec)
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
inline int log_rv_get_log_rec_offset (const T &log_rec)
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
inline rvfun::fun_t log_rv_get_fun (const T &, LOG_RCVINDEX rcvindex)
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

#endif // LOC_RECOVERY_REDO_LOG_REC_HPP
