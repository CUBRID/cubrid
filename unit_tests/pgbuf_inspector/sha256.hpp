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
 * sha256.hpp - SHA-256 for the conformance corpus checksum file
 *
 * The corpus is pinned by the digests that sha256sum prints, so the test and the corpus generator
 * need the same function without pulling a crypto library into a unit test. This is the FIPS 180-4
 * algorithm, checked against its published known answers by the unit test.
 */

#ifndef _TEST_PGBUF_INSPECTOR_SHA256_HPP_
#define _TEST_PGBUF_INSPECTOR_SHA256_HPP_

#include <string>

namespace corpus
{
  /* Lowercase hexadecimal SHA-256 digest of the bytes of data. */
  std::string sha256_hex (const std::string &data);
}

#endif /* _TEST_PGBUF_INSPECTOR_SHA256_HPP_ */
