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
 * stream_debug_echo.hpp - The echo kinds of a debug build (stream_debug_echo.cpp).
 */

#ifndef _STREAM_DEBUG_ECHO_HPP_
#define _STREAM_DEBUG_ECHO_HPP_

#if !defined (NDEBUG)

const int STREAM_KIND_DEBUG_ECHO_UPLOAD = 5;
const int STREAM_KIND_DEBUG_ECHO_DOWNLOAD = 6;

/* The server and the client library each register their half at load time; a unit test calls these itself. */
extern int stream_debug_echo_register_server (void);
extern int stream_debug_echo_register_client (void);

#endif /* !NDEBUG */

#endif /* _STREAM_DEBUG_ECHO_HPP_ */
