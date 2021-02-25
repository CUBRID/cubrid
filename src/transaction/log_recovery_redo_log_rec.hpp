#ifndef LOC_RECOVERY_REDO_LOG_REC_HPP
#define LOC_RECOVERY_REDO_LOG_REC_HPP

#include "log_reader.hpp"
#include "log_record.hpp"
#include "storage_common.h"

///* function will copy data into the provided structure
// */
//template <typename T>
//void log_rv_rec_get_log_data (log_reader &reader, T &log_data);

///* implementation assumes the reader is already correctly positioned
// */
//template <typename T>
//MVCCID log_rv_rec_get_mvccid (const T &log_rec);

///*
// * template implementations - log_rv_rec_get_log_data
// */

//template <typename T>
//void log_rv_rec_get_log_data (log_reader &reader, T &log_data)
//{
//  static_assert (sizeof (T) == 0, "should not be called");
//}

//template <>
//void log_rv_rec_get_log_data<LOG_REC_MVCC_UNDOREDO> (log_reader &reader, LOG_DATA &log_data)
//{
//  reader.advance_when_does_not_fit (sizeof (LOG_REC_MVCC_UNDOREDO));

//  /* MVCC op undo/redo log record */
//  // *INDENT-OFF*
//  const LOG_REC_MVCC_UNDOREDO *mvcc_undoredo = reader.reinterpret_cptr<LOG_REC_MVCC_UNDOREDO> ();
//  // *INDENT-ON*

//  /* Copy undoredo structure */
//  memcpy (&log_rec, &mvcc_undoredo->undoredo.data, sizeof (LOG_DATA));
//}

////template <>
////void log_rv_rec_get_log_rec<LOG_REC_UNDOREDO> (log_reader &reader, LOG_REC_UNDOREDO& log_rec)

///*
// * template implementations - log_rv_rec_get_mvccid
// */

//template <typename T>
//MVCCID log_rv_rec_get_mvccid (const T &reader)
//{
//  return MVCCID_NULL;
//}

//template <>
//MVCCID log_rv_rec_get_mvccid<LOG_REC_MVCC_UNDOREDO> (const LOG_REC_MVCC_UNDOREDO &log_rec)
//{
//  return log_rec.mvccid;
//}

#endif // LOC_RECOVERY_REDO_LOG_REC_HPP
