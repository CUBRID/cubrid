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

#ifndef _LOG_RECOVERY_ANALYSIS_CORRUPTION_CHECKER_CPP_HPP_
#define _LOG_RECOVERY_ANALYSIS_CORRUPTION_CHECKER_CPP_HPP_

#include "error_manager.h"
#include "log_common_impl.h"
#include "log_impl.h"
#include "log_lsa.hpp"
#include "log_record.hpp"
#include "system_parameter.h"
#include "tde.h"

class corruption_checker
{
    //////////////////////////////////////////////////////////////////////////
    // Does log data sanity checks and saves the results.
    //////////////////////////////////////////////////////////////////////////

  public:
    static constexpr size_t IO_BLOCK_SIZE = 4 * ONE_K;   // 4k

    corruption_checker ();

    // Check if given page checksum is correct. Otherwise page is corrupted
    void check_page_checksum (const LOG_PAGE *log_pgptr);
    // Additional checks on log record:
    void check_log_record (const log_lsa &record_lsa, const log_rec_header &record_header, const LOG_PAGE *log_page_p);
    // Detect the address of first block having only 0xFF bytes
    void find_first_corrupted_block (const LOG_PAGE *log_pgptr);

    bool is_page_corrupted () const;		      // is last checked page corrupted?
    const log_lsa &get_first_corrupted_lsa () const;  // get the address of first block full of 0xFF

  private:
    const char *get_block_ptr (const LOG_PAGE *page, size_t block_index) const;	  // the starting pointer of block

    bool m_is_page_corrupted = false;		      // true if last checked page is corrupted, false otherwise
    LOG_LSA m_first_corrupted_rec_lsa = NULL_LSA;     // set by last find_first_corrupted_block call
    std::unique_ptr<char[]> m_null_block;	      // 4k of 0xFF for null block check
    size_t m_blocks_in_page_count = 0;		      // number of blocks in a log page
};

corruption_checker::corruption_checker ()
{
  m_blocks_in_page_count = LOG_PAGESIZE / IO_BLOCK_SIZE;
  m_null_block.reset (new char[IO_BLOCK_SIZE]);
  std::memset (m_null_block.get (), LOG_PAGE_INIT_VALUE, IO_BLOCK_SIZE);
}

void
corruption_checker::check_page_checksum (const LOG_PAGE *log_pgptr)
{
  m_is_page_corrupted = !logpb_page_has_valid_checksum (log_pgptr);
}

void corruption_checker::find_first_corrupted_block (const LOG_PAGE *log_pgptr)
{
  m_first_corrupted_rec_lsa = NULL_LSA;
  for (size_t block_index = 0; block_index < m_blocks_in_page_count; ++block_index)
    {
      if (std::memcmp (get_block_ptr (log_pgptr, block_index), m_null_block.get (), IO_BLOCK_SIZE) == 0)
	{
	  // Found a block full of 0xFF
	  m_first_corrupted_rec_lsa.pageid = log_pgptr->hdr.logical_pageid;
	  m_first_corrupted_rec_lsa.offset =
		  (block_index == 0) ? 0 : (block_index * IO_BLOCK_SIZE - sizeof (LOG_HDRPAGE));
	}
    }
}

bool
corruption_checker::is_page_corrupted () const
{
  return m_is_page_corrupted;
}

const
log_lsa &corruption_checker::get_first_corrupted_lsa () const
{
  return m_first_corrupted_rec_lsa;
}

const char *
corruption_checker::get_block_ptr (const LOG_PAGE *page, size_t block_index) const
{
  return (reinterpret_cast<const char *> (page)) + block_index * IO_BLOCK_SIZE;
}

void
corruption_checker::check_log_record (const log_lsa &record_lsa, const log_rec_header &record_header,
				      const LOG_PAGE *log_page_p)
{
  if (m_is_page_corrupted)
    {
      // Already found corruption
      return;
    }

  /* For safety reason. Normally, checksum must detect corrupted pages. */
  if (LSA_ISNULL (&record_header.forw_lsa) && record_header.type != LOG_END_OF_LOG
      && !logpb_is_page_in_archive (record_lsa.pageid))
    {
      /* Can't find the end of log. The next log is null. Consider the page corrupted. */
      m_is_page_corrupted = true;
      find_first_corrupted_block (log_page_p);

      er_log_debug (ARG_FILE_LINE,
		    "log_recovery_analysis: ** WARNING: An end of the log record was not found."
		    "Latest log record at lsa = %lld|%d, first_corrupted_lsa = %lld|%d\n",
		    LSA_AS_ARGS (&record_lsa), LSA_AS_ARGS (&m_first_corrupted_rec_lsa));
    }
  else if (record_header.forw_lsa.pageid == record_lsa.pageid)
    {
      /* Quick fix. Sometimes page corruption is not detected. Check whether the current log record
       * is in corrupted block. If true, consider the page corrupted.
       */
      size_t start_block_index = (record_lsa.offset + sizeof (LOG_HDRPAGE) - 1) / IO_BLOCK_SIZE;
      assert (start_block_index <= m_blocks_in_page_count);
      size_t end_block_index = (record_header.forw_lsa.offset + sizeof (LOG_HDRPAGE) - 1) / IO_BLOCK_SIZE;
      assert (end_block_index <= m_blocks_in_page_count);

      if (start_block_index != end_block_index)
	{
	  assert (start_block_index < end_block_index);
	  if (std::memcmp (get_block_ptr (log_page_p, end_block_index), m_null_block.get (), IO_BLOCK_SIZE) == 0)
	    {
	      /* The current record is corrupted - ends into a corrupted block. */
	      m_first_corrupted_rec_lsa = record_lsa;
	      m_is_page_corrupted = true;

	      er_log_debug (ARG_FILE_LINE,
			    "log_recovery_analysis: ** WARNING: An end of the log record was not found."
			    "Latest log record at lsa = %lld|%d, first_corrupted_lsa = %lld|%d\n",
			    LSA_AS_ARGS (&record_lsa), LSA_AS_ARGS (&m_first_corrupted_rec_lsa));
	    }
	}
    }
}

#endif // _LOG_RECOVERY_ANALYSIS_CORRUPTION_CHECKER_CPP_HPP_
