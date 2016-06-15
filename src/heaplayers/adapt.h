// -*- C++ -*-

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2003 by Emery Berger
  http://www.cs.umass.edu/~emery
  emery@cs.umass.edu
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

*/


#ifndef _ADAPT_H_
#define _ADAPT_H_

/**
 * @class AdaptHeap
 * @brief Maintains dictionary entries through freed objects.
 * Sample dictionaries include DLList and SLList.
 */

namespace HL {

  template <class Dictionary, class SuperHeap>
  class AdaptHeap : public SuperHeap {
  public:

    /// Allocate an object (remove from the dictionary).
    inline void * malloc (const size_t) {
      void * ptr = (Entry *) dict.get();
      return ptr;
    }

    /// Deallocate the object (return to the dictionary).
    inline void free (void * ptr) {
      Entry * entry = (Entry *) ptr;
      dict.insert (entry);
    }

    /// Remove an object from the dictionary.
    inline int remove (void * ptr) {
      dict.remove ((Entry *) ptr);
      return 1;
    }

    /// Clear the dictionary.
    inline void clear (void) {
      Entry * ptr;
      while ((ptr = (Entry *) dict.get()) != NULL) {
	SuperHeap::free (ptr);
      }
      dict.clear();
      SuperHeap::clear();
    }


  private:

    /// The dictionary object.
    Dictionary dict;

    class Entry : public Dictionary::Entry {};
  };

}

#endif // _ADAPT_H_
