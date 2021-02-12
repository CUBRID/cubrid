
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

bool operator== (const LOG_REC_UNDOREDO &left, const LOG_REC_UNDOREDO &rite)
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

bool operator== (const LOG_ZIP &left, const LOG_ZIP &rite)
{
  const bool res1 = left.data_length == rite.data_length;
  if (res1)
    {
      const int res2 = strncmp (left.log_data, rite.log_data, left.data_length);
      return res2 == 0;
    }
  return res1;
}
