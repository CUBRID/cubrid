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
 * px_sp_eligibility.hpp - common judge for running a stored procedure inside a parallel
 *                         execution path (parallel scan / subquery / hash join checkers).
 *
 * Eligibility is the PARALLEL_ENABLE declaration bit alone. The declaration is trusted without
 * verification - a false declaration is the declarer's responsibility - and it is enforced on
 * the one point that would otherwise make parallel execution unsafe: the SP cannot reach the
 * server-side connection (ER_SP_PARALLEL_ENABLE_NO_SQL). No environment gate is needed, because
 * transaction control travels the same refused channel.
 */

#ifndef _PX_SP_ELIGIBILITY_HPP_
#define _PX_SP_ELIGIBILITY_HPP_

#include "pl_signature.hpp"

inline bool
px_sp_is_parallel_eligible (const cubpl::pl_signature *sig)
{
  return sig != nullptr && sig->is_parallel_enabled;
}

#endif /* _PX_SP_ELIGIBILITY_HPP_ */
