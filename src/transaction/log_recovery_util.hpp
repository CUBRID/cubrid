#ifndef LOG_RECOVERY_UTIL_HPP
#define LOG_RECOVERY_UTIL_HPP

#include <memory>
#include "log_record.hpp"
#include "log_storage.hpp"
#include "log_compress.h"

/* helper alias to RAII a malloc'ed sequence of bytes
 *
 */
template <typename T>
using raii_blob = std::unique_ptr<T, decltype (::free) *>;

/* TODO: temporary ops needed for debug
 */

extern bool operator== (const vfid &left, const vfid &rite);
extern bool operator== (const log_vacuum_info &left, const log_vacuum_info &rite);
extern bool operator== (const LOG_RECORD_HEADER &left, const LOG_RECORD_HEADER &rite);
extern bool operator== (const log_data &left, const log_data &rite);
extern bool operator== (const LOG_REC_UNDOREDO &left, const LOG_REC_UNDOREDO &rite);
extern bool operator== (const log_hdrpage &left, const log_hdrpage &rite);
extern bool operator== (const log_rec_mvcc_undoredo &left, const log_rec_mvcc_undoredo &rite);
extern bool operator== (const struct log_zip &left, const struct log_zip &rite);
extern bool operator== (const log_rec_redo &left, const log_rec_redo &rite);
extern bool operator== (const log_rec_mvcc_redo &left, const log_rec_mvcc_redo &rite);
extern bool operator== (const log_rec_dbout_redo &left, const log_rec_dbout_redo &rite);
extern bool operator== (const log_rec_run_postpone &left, const log_rec_run_postpone &rite);
extern bool operator== (const log_rec_compensate &left, const log_rec_compensate &rite);
extern bool operator== (const log_rec_undo &left, const log_rec_undo &rite);
extern bool operator== (const log_rec_mvcc_undo &left, const log_rec_mvcc_undo &rite);
extern bool operator== (const log_rec_sysop_end &left, const log_rec_sysop_end &rite);
extern bool operator== (const log_rcv &left, const log_rcv &rite);

#endif // LOG_RECOVERY_UTIL_HPP
