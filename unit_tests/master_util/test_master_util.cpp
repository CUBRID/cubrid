/*
 * Copyright 2024 CUBRID Corporation
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
 * test_master_util.cpp - CBRD-27511: unit tests for the cub_master exec_path
 *                        whitelist (master_util_exec_path_is_trusted).
 *
 * The trusted prefix is derived at run time from envvar_bindir_file() so the
 * test is independent of the actual CUBRID installation path.
 */

#include "master_util.h"
#include "environment_variable.h"

#include <cstdio>
#include <climits>
#include <string>

static int g_failures = 0;

static void
check (const char *label, bool got, bool expected)
{
  const bool ok = (got == expected);
  if (!ok)
    {
      g_failures++;
    }
  std::printf ("[%s] %s (got=%s, expected=%s)\n", ok ? "PASS" : "FAIL", label,
	       got ? "true" : "false", expected ? "true" : "false");
}

int
main (void)
{
  char bindir[PATH_MAX];

  bindir[0] = '\0';
  (void) envvar_bindir_file (bindir, sizeof (bindir), "");
  std::printf ("trusted bin dir prefix: \"%s\"\n\n", bindir);

  if (bindir[0] == '\0')
    {
      std::printf ("FAIL: could not resolve the bin directory (is $CUBRID set?)\n");
      return 1;
    }

  const std::string prefix (bindir);
  const std::string good_server = prefix + "cub_server";
  const std::string good_cubrid = prefix + "cubrid";
  const std::string bad_traversal = prefix + "../etc/passwd";
  const std::string bad_subdir = prefix + "sub/evil";
  const std::string bad_only_dir = prefix;	/* no file name after bin/ */

  /* a legitimate server binary living directly in <CUBRID>/bin/ is trusted */
  check ("binary directly in bin/ (cub_server)", master_util_exec_path_is_trusted (good_server.c_str ()), true);
  check ("binary directly in bin/ (cubrid)", master_util_exec_path_is_trusted (good_cubrid.c_str ()), true);

  /* everything else must be rejected */
  check ("parent-directory traversal is rejected", master_util_exec_path_is_trusted (bad_traversal.c_str ()), false);
  check ("nested subdirectory under bin/ is rejected", master_util_exec_path_is_trusted (bad_subdir.c_str ()), false);
  check ("path outside the trusted bin dir is rejected", master_util_exec_path_is_trusted ("/usr/bin/id"), false);
  check ("relative path is rejected", master_util_exec_path_is_trusted ("cub_server"), false);
  check ("bin dir with no file name is rejected", master_util_exec_path_is_trusted (bad_only_dir.c_str ()), false);
  check ("empty path is rejected", master_util_exec_path_is_trusted (""), false);
  check ("null path is rejected", master_util_exec_path_is_trusted (NULL), false);

  if (g_failures == 0)
    {
      std::printf ("\nAll master_util_exec_path_is_trusted tests passed.\n");
      return 0;
    }

  std::printf ("\n%d test(s) FAILED.\n", g_failures);
  return 1;
}
