// -*- C++ -*-

#ifndef HL_ISTRUE_H
#define HL_ISTRUE_H

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure
  
  Copyright (C) 2000-2020 by Emery Berger
  http://www.emeryberger.com
  emery@cs.umass.edu
  
  Heap Layers is distributed under the terms of the Apache 2.0 license.

  You may obtain a copy of the License at
  http://www.apache.org/licenses/LICENSE-2.0

*/

namespace HL {

  template <bool isTrue>
  class IsTrue;

  template<>
  class IsTrue<true> {
  public:
    enum { value = true };
  };

  template<>
  class IsTrue<false> {
  public:
    enum { value = false };
  };

}

#endif
