/*
 *
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
 * px_heap_scan_ftab_set.hpp
 */

#ifndef _PX_HEAP_SCAN_FTAB_SET_HPP_
#define _PX_HEAP_SCAN_FTAB_SET_HPP_

#include "file_manager.h"


namespace parallel_heap_scan
{
  class ftab_set
  {
    private:
      std::vector<FILE_PARTIAL_SECTOR> m_ftab_set;
      size_t iterator;

    public:
      ftab_set()
	:m_ftab_set(),
	 iterator (0)
      {}

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
	if (iterator >= m_ftab_set.size())
	  {
	    return FILE_PARTIAL_SECTOR_INITIALIZER;
	  }
	FILE_PARTIAL_SECTOR ftab = m_ftab_set[iterator];
	iterator++;
	return ftab;
      }

      void clear()
      {
	m_ftab_set.clear();
	iterator = 0;
      }
  };
}

#endif /*_PX_HEAP_SCAN_FTAB_SET_HPP_ */
