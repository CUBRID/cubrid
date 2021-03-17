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

#include "log_checkpoint_info.hpp"

namespace cublog
{

  int
  log_lsa_size (cubpacking::packer &serializator, std::size_t start_offset, std::size_t size_arg)
  {
    size_t size = size_arg;
    size += serializator.get_packed_bigint_size (start_offset + size);
    size += serializator.get_packed_short_size (start_offset + size);

    return size;
  }

  void
  log_lsa_pack (LOG_LSA log, cubpacking::packer &serializator)
  {
    serializator.pack_bigint (log.pageid);
    serializator.pack_short (log.offset);
  }

  LOG_LSA
  log_lsa_unpack (cubpacking::unpacker &deserializator)
  {
    int64_t pageid;
    short offset;
    deserializator.unpack_bigint (pageid);
    deserializator.unpack_short  (offset);

    return log_lsa (pageid, offset);
  }

  void
  checkpoint_info::pack (cubpacking::packer &serializator) const
  {

    log_lsa_pack (m_start_redo_lsa, serializator);
    log_lsa_pack (m_snapshot_lsa, serializator);

    serializator.pack_bigint (m_trans.size ());
    for (const checkpoint_tran_info tran_info : m_trans)
      {
	serializator.pack_int (tran_info.isloose_end);
	serializator.pack_int (tran_info.trid);
	serializator.pack_int (tran_info.state);
	log_lsa_pack (tran_info.head_lsa, serializator);
	log_lsa_pack (tran_info.tail_lsa, serializator);
	log_lsa_pack (tran_info.undo_nxlsa, serializator);

	log_lsa_pack (tran_info.posp_nxlsa, serializator);
	log_lsa_pack (tran_info.savept_lsa, serializator);
	log_lsa_pack (tran_info.tail_topresult_lsa, serializator);
	log_lsa_pack (tran_info.start_postpone_lsa, serializator);
	serializator.pack_c_string (tran_info.user_name, strlen (tran_info.user_name));
      }

    serializator.pack_bigint (m_sysops.size ());
    for (const checkpoint_sysop_info sysop_info : m_sysops)
      {
	serializator.pack_int (sysop_info.trid);
	log_lsa_pack (sysop_info.sysop_start_postpone_lsa, serializator);
	log_lsa_pack (sysop_info.atomic_sysop_start_lsa, serializator);
      }

    serializator.pack_bool (m_has_2pc);
  }

  void
  checkpoint_info::unpack (cubpacking::unpacker &deserializator)
  {
    m_start_redo_lsa = log_lsa_unpack (deserializator);
    m_snapshot_lsa = log_lsa_unpack (deserializator);

    std::uint64_t trans_size = 0;
    deserializator.unpack_bigint (trans_size);
    for (uint i = 0; i < trans_size; i++)
      {
	LOG_INFO_CHKPT_TRANS chkpt_trans;

	deserializator.unpack_int (chkpt_trans.isloose_end);
	deserializator.unpack_int (chkpt_trans.trid);

	deserializator.unpack_from_int (chkpt_trans.state);
	chkpt_trans.head_lsa = log_lsa_unpack (deserializator);
	chkpt_trans.tail_lsa = log_lsa_unpack (deserializator);
	chkpt_trans.undo_nxlsa = log_lsa_unpack (deserializator);

	chkpt_trans.posp_nxlsa = log_lsa_unpack (deserializator);
	chkpt_trans.savept_lsa = log_lsa_unpack (deserializator);
	chkpt_trans.tail_topresult_lsa = log_lsa_unpack (deserializator);
	chkpt_trans.start_postpone_lsa = log_lsa_unpack (deserializator);
	deserializator.unpack_c_string (chkpt_trans.user_name, LOG_USERNAME_MAX);

	m_trans.push_back (chkpt_trans);

      }

    std::uint64_t sysop_size = 0;
    deserializator.unpack_bigint (sysop_size);
    for (uint i = 0; i < sysop_size; i++)
      {
	LOG_INFO_CHKPT_SYSOP chkpt_sysop;
	deserializator.unpack_int (chkpt_sysop.trid);
	chkpt_sysop.sysop_start_postpone_lsa = log_lsa_unpack (deserializator);
	chkpt_sysop.atomic_sysop_start_lsa = log_lsa_unpack (deserializator);

	m_sysops.push_back (chkpt_sysop);
      }

    deserializator.unpack_bool (m_has_2pc);
  }

  size_t
  checkpoint_info::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size =  0;
    size = log_lsa_size (m_start_redo_lsa, serializator, start_offset, size);
    size = log_lsa_size (m_snapshot_lsa, serializator, start_offset, size);

    size += serializator.get_packed_bigint_size (start_offset + size);
    for (const checkpoint_tran_info tran_info : m_trans)
      {
	size += serializator.get_packed_int_size (start_offset + size);
	size += serializator.get_packed_int_size (start_offset + size);
	size += serializator.get_packed_int_size (start_offset + size);

	size = log_lsa_size (tran_info.head_lsa, serializator, start_offset, size);
	size = log_lsa_size (tran_info.tail_lsa, serializator, start_offset, size);
	size = log_lsa_size (tran_info.undo_nxlsa, serializator, start_offset, size);

	size = log_lsa_size (tran_info.posp_nxlsa, serializator, start_offset, size);
	size = log_lsa_size (tran_info.savept_lsa, serializator, start_offset, size);
	size = log_lsa_size (tran_info.tail_topresult_lsa, serializator, start_offset, size);
	size = log_lsa_size (tran_info.start_postpone_lsa, serializator, start_offset, size);
	size += serializator.get_packed_c_string_size (tran_info.user_name, strlen (tran_info.user_name), start_offset + size);
      }

    size += serializator.get_packed_bigint_size (start_offset + size);
    for (const checkpoint_sysop_info sysop_info : m_sysops)
      {
	size += serializator.get_packed_int_size (start_offset + size);
	size = log_lsa_size (sysop_info.sysop_start_postpone_lsa, serializator, start_offset, size);
	size = log_lsa_size (sysop_info.atomic_sysop_start_lsa, serializator, start_offset, size);
      }

    size += serializator.get_packed_bool_size (start_offset + size);

    return size;
  }

  void checkpoint_info::load_trantable_snapshot ()
  {
  }

}

