#include <stdio.h>
#include "db.h"
#include "authenticate.h"

extern float log_get_db_compatibility();

int main(int argc, char * argv[])
{
  float disk_compat_level = 0.0f;
  char * prog_name;
  char * db_name;
  int retval;

  if (argc < 2) {
    return 1;
  }

  prog_name = argv[0];
  db_name = argv[1];

  AU_DISABLE_PASSWORDS();

  db_login("dba", NULL);
  retval = db_restart (prog_name, 0, db_name);
  if (retval != NO_ERROR && retval != ER_LOG_INCOMPATIBLE_DATABASE)
    {
      printf("%s\n", db_error_string(3));
      return 1;
    }

  disk_compat_level = log_get_db_compatibility();

  db_shutdown();

  if (disk_compat_level < 1.7) {
    printf("incompatible version\n");
    return 1;
  }
  else if (disk_compat_level <= 1.8) {
    printf("6_6\n");
  }
  else if (disk_compat_level <= 1.9) {
    printf("7_1\n");
  }
  else if (disk_compat_level <= 2.0) {
    printf("7_3\n");
  }
  else if (disk_compat_level > 2.0) {
    printf("Don't need to migrate.\n");
  }
  else {
    printf("Database version infomation is not available.\n");
    return 1;
  }

  return 0;
}

