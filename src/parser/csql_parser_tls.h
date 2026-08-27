/*
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
 * csql_parser_tls.h - thread-storage marker for the SQL parser's per-parse
 *                     state (wf122 track A, stage A2)
 *
 * Under SERVER_MODE the merged-in client half parses SQL on server worker
 * threads, so every piece of per-parse state that historically lived in a
 * process-wide global must become per-thread.  A parse never migrates
 * between threads (the statement bracket pins one worker), so thread_local
 * is the correct scope for state whose lifetime is a single yyparse.
 *
 * Outside SERVER_MODE the macro expands to nothing: the CS/SA libraries
 * keep their one-parser-per-process globals bit-identical, paying no TLS
 * access cost (each read in a .so is a __tls_get_addr call).
 *
 * cmake/patch_parser_tls.cmake stamps this same marker onto the mutable
 * globals bison and flex generate; keep the two in sync.
 */

#ifndef _CSQL_PARSER_TLS_H_
#define _CSQL_PARSER_TLS_H_

#if defined (SERVER_MODE)
#if defined (__cplusplus)
#define CSQL_PARSER_TLS thread_local
#else
#define CSQL_PARSER_TLS _Thread_local
#endif
#else /* !SERVER_MODE */
#define CSQL_PARSER_TLS
#endif /* !SERVER_MODE */

#endif /* _CSQL_PARSER_TLS_H_ */
