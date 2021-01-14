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
 * cubrid_version.c - Small utility to print release string
 */

#ident "$Id$"


#include <stdio.h>
#include "release_string.h"

int
main (int argc, char *argv[])
{
  char buf[REL_MAX_VERSION_LENGTH];

  rel_copy_version_string (buf, REL_MAX_VERSION_LENGTH);
  fprintf (stdout, "\n%s\n\n", buf);
  return 0;
}
