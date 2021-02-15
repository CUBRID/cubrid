
#include "log_recovery_util.hpp"

bool operator== (const vfid &left, const vfid &rite)
{
  return left.fileid == rite.fileid
	 && left.volid == rite.volid;
}

bool operator== (const log_vacuum_info &left, const log_vacuum_info &rite)
{
  return left.prev_mvcc_op_log_lsa == rite.prev_mvcc_op_log_lsa
	 && left.vfid == rite.vfid;
}

bool operator== (const LOG_RECORD_HEADER &left, const LOG_RECORD_HEADER &rite)
{
  return left.prev_tranlsa == rite.prev_tranlsa
	 && left.back_lsa == rite.back_lsa
	 && left.forw_lsa == rite.forw_lsa
	 && left.trid == rite.trid
	 && left.type == rite.type;
}

bool operator== (const log_data &left, const log_data &rite)
{
  return left.rcvindex == rite.rcvindex
	 && left.pageid == rite.pageid
	 && left.offset == rite.offset
	 && left.volid == rite.volid;
}

bool operator== (const log_rec_undoredo &left, const log_rec_undoredo &rite)
{
  return left.data == rite.data
	 && left.ulength == rite.ulength
	 && left.rlength == rite.rlength;
}

bool operator== (const log_hdrpage &left, const log_hdrpage &rite)
{
  return left.logical_pageid == rite.logical_pageid
	 && left.offset == rite.offset
	 && left.flags == rite.flags
	 && left.checksum == rite.checksum;
}

bool operator== (const log_rec_mvcc_undoredo &left, const log_rec_mvcc_undoredo &rite)
{
  return left.undoredo == rite.undoredo
	 && left.mvccid == rite.mvccid
	 && left.vacuum_info == rite.vacuum_info;
}

bool operator== (const struct log_zip &left, const struct log_zip &rite)
{
  const bool res1 = left.data_length == rite.data_length;
  if (res1)
    {
      const int res2 = strncmp (left.log_data, rite.log_data, left.data_length);
      return res2 == 0;
    }
  return res1;
}

bool operator== (const log_rec_redo &left, const log_rec_redo &rite)
{
  return left.data == rite.data
	 && left.length == rite.length;
}

bool operator == (const log_rec_mvcc_redo &left, const log_rec_mvcc_redo &rite)
{
  return left.redo == rite.redo
	 && left.mvccid == rite.mvccid;
}

bool operator == (const log_rec_dbout_redo &left, const log_rec_dbout_redo &rite)
{
  return left.rcvindex == rite.rcvindex
	 && left.length == rite.length;
}

bool operator== (const log_rec_run_postpone &left, const log_rec_run_postpone &rite)
{
  return left.data == rite.data
	 && left.ref_lsa == rite.ref_lsa
	 && left.length == rite.length;
}

bool operator== (const log_rec_compensate &left, const log_rec_compensate &rite)
{
  return left.data == rite.data
	 && left.undo_nxlsa == rite.undo_nxlsa
	 && left.length == rite.length;
}

bool operator== (const log_rec_undo &left, const log_rec_undo &rite)
{
  return left.data == rite.data
	 && left.length == rite.length;
}

bool operator== (const log_rec_mvcc_undo &left, const log_rec_mvcc_undo &rite)
{
  return left.undo == rite.undo
	 && left.mvccid == rite.mvccid
	 && left.vacuum_info == rite.vacuum_info;
}

bool operator== (const log_rec_sysop_end &left, const log_rec_sysop_end &rite)
{
  const bool res1 = left.lastparent_lsa == rite.lastparent_lsa
		    && left.prv_topresult_lsa == rite.prv_topresult_lsa
		    && left.type == rite.type;
//		    && ((left.vfid == nullptr && rite.vfid == nullptr)
//			|| (left.vfid != nullptr && rite.vfid != nullptr && * (left.vfid) == * (rite.vfid)));

  if (res1)
    {
      bool res2 = false;

      switch (left.type)
	{
	case LOG_SYSOP_END_LOGICAL_UNDO:
	  res2 = left.undo == rite.undo;
	  break;
	case LOG_SYSOP_END_LOGICAL_MVCC_UNDO:
	  res2 = left.mvcc_undo == rite.mvcc_undo;
	  break;
	case LOG_SYSOP_END_LOGICAL_COMPENSATE:
	  res2 = left.compensate_lsa == rite.compensate_lsa;
	  break;
	case LOG_SYSOP_END_LOGICAL_RUN_POSTPONE:
	  res2 = left.run_postpone.postpone_lsa == rite.run_postpone.postpone_lsa
		 && left.run_postpone.is_sysop_postpone == rite.run_postpone.is_sysop_postpone;
	  break;
	default:
	  res2 = true;
	  break;
	}

      return res2;
    }
  return res1;
}

bool operator== (const log_rcv &left, const log_rcv &rite)
{
  return left.mvcc_id == rite.mvcc_id
	 && left.pgptr == rite.pgptr
	 && left.offset == rite.offset
	 && left.length == rite.length
	 //&& left.data == rite.data
	 && left.reference_lsa == rite.reference_lsa;
}

