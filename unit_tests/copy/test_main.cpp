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

#include "test_copy_binary_decoder.hpp"
#include "language_support.h"
#include "error_manager.h"

#include <iostream>

template <typename Func>
int
test_module (int &global_error, Func &&f, const char *name)
{
  int err = f ();
  if (err == 0)
    {
      std::cout << "  PASS: " << name << std::endl;
    }
  else
    {
      std::cout << "  FAIL: " << name << std::endl;
      global_error = global_error == 0 ? err : global_error;
    }
  return err;
}

int
main ()
{
  int global_error = 0;

  /* initialize error manager and language support for db_make_varchar */
  er_init (NULL, ER_NEVER_EXIT);
  lang_init ();
  lang_set_charset_lang ("en_US.utf8");

  std::cout << "=== COPY binary decoder unit tests ===" << std::endl;

  test_module (global_error, test_copy_binary_decoder::test_decode_int, "decode INT");
  test_module (global_error, test_copy_binary_decoder::test_decode_bigint, "decode BIGINT");
  test_module (global_error, test_copy_binary_decoder::test_decode_float, "decode FLOAT");
  test_module (global_error, test_copy_binary_decoder::test_decode_double, "decode DOUBLE");
  test_module (global_error, test_copy_binary_decoder::test_decode_varchar, "decode VARCHAR");
  test_module (global_error, test_copy_binary_decoder::test_decode_null, "decode NULL");
  test_module (global_error, test_copy_binary_decoder::test_decode_multi_column, "decode multi-column row");
  test_module (global_error, test_copy_binary_decoder::test_decode_footer_sentinel, "decode footer sentinel");
  test_module (global_error, test_copy_binary_decoder::test_decode_truncated_header, "decode truncated header");
  test_module (global_error, test_copy_binary_decoder::test_decode_truncated_field, "decode truncated field");
  test_module (global_error, test_copy_binary_decoder::test_decode_field_count_mismatch, "decode field count mismatch");
  test_module (global_error, test_copy_binary_decoder::test_decode_int_wrong_size, "decode INT wrong size");
  test_module (global_error, test_copy_binary_decoder::test_decode_vector, "decode VECTOR");
  test_module (global_error, test_copy_binary_decoder::test_decode_int_vector_row, "decode INT+VECTOR row");

  std::cout << std::endl;
  if (global_error == 0)
    {
      std::cout << "All tests passed." << std::endl;
    }
  else
    {
      std::cout << "Some tests FAILED." << std::endl;
    }

  return global_error;
}
