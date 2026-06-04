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

/*
 * ftab_set.hpp
 */

#ifndef _FTAB_SET_HPP_
#define _FTAB_SET_HPP_

#include "file_manager.h"


class ftab_set
{
  private:
    std::vector<FILE_PARTIAL_SECTOR> m_ftab_set;
    size_t m_iterator;

  public:
    ftab_set()
      :m_ftab_set(),
       m_iterator (0)
    {}

    ~ftab_set()
    {
      m_ftab_set.clear();
    }


    ftab_set (const ftab_set &other)
      :m_ftab_set (other.m_ftab_set),
       m_iterator (other.m_iterator)
    {}

    ftab_set (ftab_set &&other)
      :m_ftab_set (std::move (other.m_ftab_set)),
       m_iterator (other.m_iterator)
    {
      other.m_iterator = 0;
    }

    ftab_set &operator= (const ftab_set &other)
    {
      if (this != &other)
	{
	  m_ftab_set = other.m_ftab_set;
	  m_iterator = other.m_iterator;
	}
      return *this;
    }

    ftab_set &operator= (ftab_set &&other)
    {
      if (this != &other)
	{
	  m_ftab_set = std::move (other.m_ftab_set);
	  m_iterator = other.m_iterator;
	  other.m_iterator = 0;
	}
      return *this;
    }

    void append (const ftab_set &other)
    {
      m_ftab_set.insert (m_ftab_set.end (), other.m_ftab_set.begin (), other.m_ftab_set.end ());
    }

    void convert (FILE_FTAB_COLLECTOR *ftab_collector)
    {
      int i;
      m_ftab_set.resize (ftab_collector->nsects);
      for (i = 0; i < ftab_collector->nsects; i++)
	{
	  FILE_PARTIAL_SECTOR ftab;
	  ftab = ftab_collector->partsect_ftab[i];
	  m_ftab_set[i] = ftab;
	}
    }

    std::vector<ftab_set> split (int n_sets)
    {
      std::vector<ftab_set> sets;
      if (n_sets <= 0)
	{
	  return sets;
	}

      size_t size = m_ftab_set.size();
      size_t n_elements_per_set = size / (size_t)n_sets;
      size_t remainder = size % (size_t)n_sets;
      size_t start_idx = 0;

      for (size_t i = 0; i < (size_t)n_sets; i++)
	{
	  size_t current_set_size = n_elements_per_set + (i < remainder ? 1 : 0);
	  ftab_set set;
	  set.m_ftab_set = std::vector<FILE_PARTIAL_SECTOR> (m_ftab_set.begin () + start_idx,
			   m_ftab_set.begin () + start_idx + current_set_size);
	  sets.push_back (set);
	  start_idx += current_set_size;
	}
      return sets;
    }

    FILE_PARTIAL_SECTOR get_next()
    {
      if (m_iterator >= m_ftab_set.size())
	{
	  return FILE_PARTIAL_SECTOR_INITIALIZER;
	}
      FILE_PARTIAL_SECTOR ftab = m_ftab_set[m_iterator];
      m_iterator++;
      return ftab;
    }

    size_t size() const
    {
      return m_ftab_set.size();
    }

    void clear()
    {
      m_ftab_set.clear();
      m_iterator = 0;
    }
};

// table-wide pick over N partitions: one stride over merged sectors, per-partition offsets
int collect_strided_vpids_multi (THREAD_ENTRY *thread_p,
				 const HFID *hfids,
				 int n_hfids,
				 VPID **out_picked,
				 int *out_count,
				 int **out_part_offsets,
				 int *out_weight);

#endif // _FTAB_SET_HPP_
