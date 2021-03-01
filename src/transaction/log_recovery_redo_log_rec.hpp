#ifndef LOC_RECOVERY_REDO_LOG_REC_HPP
#define LOC_RECOVERY_REDO_LOG_REC_HPP

#include "log_reader.hpp"
#include "log_record.hpp"
#include "recovery.h"
#include "storage_common.h"

/* function will copy data into the provided structure
 */
template <typename T>
const LOG_DATA &log_rv_get_log_rec_data (const T &log_rec);

/* implementation assumes the reader is already correctly positioned
 */
template <typename T>
MVCCID log_rv_get_log_rec_mvccid (const T &log_rec);

/*
 */
template <typename T>
VPID log_rv_get_log_rec_vpid (const T &log_rec);

///*
// */
//template <typename T>
//int log_rv_get_log_rec_undo_length (const T &log_rec);

/*
 */
template <typename T>
int log_rv_get_log_rec_redo_length (const T &log_rec);

/*
 * TODO: maybe this function is not needed as offset is always part of the log_rec - data
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
  static const LOG_DATA dummy;
  return dummy;
}

template <>
const LOG_DATA &log_rv_get_log_rec_data<LOG_REC_MVCC_UNDOREDO> (const LOG_REC_MVCC_UNDOREDO &log_rec)
{
  return log_rec.undoredo.data;
}

template <>
const LOG_DATA &log_rv_get_log_rec_data<LOG_REC_UNDOREDO> (const LOG_REC_UNDOREDO &log_rec)
{
  return log_rec.data;
}

template <typename T>
MVCCID log_rv_get_log_rec_mvccid (const T &)
{
  return MVCCID_NULL;
}

template <>
MVCCID log_rv_get_log_rec_mvccid<LOG_REC_MVCC_UNDOREDO> (const LOG_REC_MVCC_UNDOREDO &log_rec)
{
  return log_rec.mvccid;
}
// TODO: other..

template <typename T>
VPID log_rv_get_log_rec_vpid (const T &log_rec)
{
  static_assert (sizeof (T) == 0, "should not be called");
  return VPID_INITIALIZER;
}

template <>
VPID log_rv_get_log_rec_vpid<LOG_REC_MVCC_UNDOREDO> (const LOG_REC_MVCC_UNDOREDO &log_rec)
{
  return
  {
    log_rec.undoredo.data.pageid,
    log_rec.undoredo.data.volid
  };
}

template <>
VPID log_rv_get_log_rec_vpid<LOG_REC_UNDOREDO> (const LOG_REC_UNDOREDO &log_rec)
{
  return
  {
    log_rec.data.pageid,
    log_rec.data.volid
  };
}

//template <typename T>
//int log_rv_get_log_rec_undo_length (const T &log_rec)
//{
//  static_assert (sizeof (T) == 0, "should not be called");
//}

//template <>
//int log_rv_get_log_rec_undo_length<LOG_REC_MVCC_UNDOREDO> (const LOG_REC_MVCC_UNDOREDO &log_rec)
//{
//  return log_rec.undoredo.ulength;
//}
//// TODO: other..

template <typename T>
int log_rv_get_log_rec_redo_length (const T &log_rec)
{
  static_assert (sizeof (T) == 0, "should not be called");
  return -1;
}

template <>
int log_rv_get_log_rec_redo_length<LOG_REC_MVCC_UNDOREDO> (const LOG_REC_MVCC_UNDOREDO &log_rec)
{
  return log_rec.undoredo.rlength;
}

template <>
int log_rv_get_log_rec_redo_length<LOG_REC_UNDOREDO> (const LOG_REC_UNDOREDO &log_rec)
{
  return log_rec.rlength;
}

template <typename T>
int log_rv_get_log_rec_offset (const T &log_rec)
{
  static_assert (sizeof (T) == 0, "should not be called");
  return -1;
}

template <>
int log_rv_get_log_rec_offset<LOG_REC_MVCC_UNDOREDO> (const LOG_REC_MVCC_UNDOREDO &log_rec)
{
  return log_rec.undoredo.data.offset;
}

template <>
int log_rv_get_log_rec_offset<LOG_REC_UNDOREDO> (const LOG_REC_UNDOREDO &log_rec)
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
rvfun::fun_t log_rv_get_fun<LOG_REC_MVCC_UNDOREDO> (const LOG_REC_MVCC_UNDOREDO &, LOG_RCVINDEX rcvindex)
{
  return RV_fun[rcvindex].redofun;
}

template <>
rvfun::fun_t log_rv_get_fun<LOG_REC_UNDOREDO> (const LOG_REC_UNDOREDO &, LOG_RCVINDEX rcvindex)
{
  return RV_fun[rcvindex].redofun;
}

#endif // LOC_RECOVERY_REDO_LOG_REC_HPP
