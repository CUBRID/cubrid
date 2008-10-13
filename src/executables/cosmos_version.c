/*
 * Copyright (C) 2008 NHN Corporation
 * Copyright (C) 2008 CUBRID Co., Ltd.
 *
 * usql_rel.c - Small utility to print release string
 *
 */

#ident "$Id$"


#include <stdio.h>
#include "release_string.h"

int
main (int argc, char *argv[])
{
  fprintf (stdout, "\n%s (%s)\n\n", rel_name (), rel_build_number ());
  return 0;
}
