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
 * server_compile_tracer.hpp - in-process compile tracer (see .cpp)
 */

#ifndef _SERVER_COMPILE_TRACER_HPP_
#define _SERVER_COMPILE_TRACER_HPP_

#if defined (SERVER_MODE)
// spawns a detached tracer thread iff env CUBRID_M0_TRACER_SQL is set
void boot_tracer_start_if_requested (const char *server_name);
#endif

#endif /* _SERVER_COMPILE_TRACER_HPP_ */
