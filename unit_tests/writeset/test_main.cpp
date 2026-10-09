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

#define CATCH_CONFIG_RUNNER
#include "catch2/catch.hpp"

#include "area_alloc.h"
#include "error_manager.h"
#include "language_support.h"
#include "object_domain.h"
#include "thread_manager.hpp"

int
main (int argc, char *argv[])
{
  /* The REF collection tests cast values through the type system and resolve parameterized
   * CHAR/NUMERIC domains. Bring up the minimal error/language/area/domain runtime first. */
  (void) er_init (NULL, ER_NEVER_EXIT);
  (void) lang_init ();
  (void) lang_set_charset_lang ("en_US.utf8");
  area_init ();
  tp_init ();
  /* NUMERIC scale conversion tables are initialized inside the numeric module on
   * develop, so no explicit table-init call is needed here. */

  /* the cast path allocates through db_private_alloc, which resolves the current
   * THREAD_ENTRY from thread-local storage; give this process a main entry the
   * same way the packing unit test does */
  cubthread::entry *thread_p = NULL;
  cubthread::initialize (thread_p);
  (void) cubthread::initialize_thread_entries ();

  int result = Catch::Session ().run (argc, argv);

  tp_final ();
  area_final ();
  return result;
}
