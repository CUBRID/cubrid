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

//
// method_compile.hpp - define structures used by method feature
//

#ifndef _METHOD_COMPILE_HPP_
#define _METHOD_COMPILE_HPP_

#include "method_invoke.hpp"

#include "method_runtime_context.hpp"
#include "mem_block.hpp"

#include <string>

namespace cubmethod
{
  int invoke_compile (runtime_context &ctx, const std::string &program, const bool &verbose,
		      cubmem::extensible_block &blk);
}

#endif //_METHOD_COMPILE_HPP_
