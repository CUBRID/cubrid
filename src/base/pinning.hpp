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
 * pinning.hpp
 */

#ifndef _PINNING_HPP_
#define _PINNING_HPP_

#ident "$Id$"

#include <set>
#include <assert.h>
#include <cstddef>
#include "error_code.h"

namespace cubbase
{
  class pinnable;

  class pinner
  {
    public:
      int pin (pinnable *reference);
      int unpin (pinnable *reference);

      int unpin_all (void);

      bool check_references (void)
      {
	return (references.size () == 0);
      };

      ~pinner ()
      {
	assert (check_references () == true);
      };

    private:
      std::set <pinnable *> references;
  };

  class pinnable
  {
    public:
      int add_pinner (pinner *referencer)
      {
	pinners.insert (referencer);
	return NO_ERROR;
      }

      int remove_pinner (pinner *referencer)
      {
	pinners.erase (referencer);
	return NO_ERROR;
      }

      int get_pin_count (void)
      {
	return (int) pinners.size ();
      }

      ~pinnable ()
      {
	assert (pinners.size () == 0);
      }

    private:
      std::set <pinner *> pinners;
  };

} /* namespace cubbase */

#endif /* _PINNING_HPP_ */
