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

#include <cstddef>
#include <utility>
#include <vector>

#include "bit.h"
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

    void convert (const FILE_FTAB_COLLECTOR *ftab_collector)
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

      /* contiguous runs of sectors with about the same number of pages each: a sector that lost its pages (a file
       * keeps its sectors after its pages are deallocated) is no work, so equal sector counts are not equal work */
      size_t size = m_ftab_set.size();
      size_t total_pages = 0;
      for (const FILE_PARTIAL_SECTOR &sector : m_ftab_set)
	{
	  total_pages += (size_t) bit64_count_ones (sector.page_bitmap);
	}

      size_t start_idx = 0;
      size_t pages_done = 0;
      for (size_t i = 0; i < (size_t) n_sets; i++)
	{
	  size_t end_idx = start_idx;
	  if (i + 1 == (size_t) n_sets)
	    {
	      end_idx = size;
	    }
	  else
	    {
	      /* up to this set's share of the pages; a sector goes to the set its first page falls in */
	      size_t pages_goal = total_pages * (i + 1) / (size_t) n_sets;
	      while (end_idx < size && pages_done < pages_goal)
		{
		  pages_done += (size_t) bit64_count_ones (m_ftab_set[end_idx].page_bitmap);
		  end_idx++;
		}
	    }
	  ftab_set set;
	  set.m_ftab_set = std::vector<FILE_PARTIAL_SECTOR> (m_ftab_set.begin () + start_idx, m_ftab_set.begin () + end_idx);
	  sets.push_back (set);
	  start_idx = end_idx;
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


#endif // _FTAB_SET_HPP_
