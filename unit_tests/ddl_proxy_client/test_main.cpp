#include "process_util.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <iostream>

#if defined(WINDOWS)
#include <direct.h>
#else
#include <unistd.h>
#endif

int main (int argc, char **argv)
{
  int exit_status, rc;
  char path[257];
  char *cubrid_env_var;
  char *cubrid_databases_env_var;
  std::string command = "";
  const char *ddl_argv[6] = {path,
			     "-udba",
			     "test_ddl_db",
			     "-c",
			     "create table t1 (a int)",
			     NULL
			    };

  cubrid_env_var = getenv ("CUBRID");
  assert (cubrid_env_var != NULL);

  cubrid_databases_env_var = getenv ("CUBRID_DATABASES");
  assert (cubrid_databases_env_var != NULL);

  strncpy (path, cubrid_env_var, 128);
  strncat (path, "/bin/ddl_proxy_client", 128);

  rc = chdir (cubrid_databases_env_var);
  assert (rc == 0);

  rc = system ("cubrid deletedb test_ddl_db");
  assert (rc != -1);
  rc = system ("cubrid createdb test_ddl_db en_US");
  assert (rc != -1);

  rc = system ("cubrid server start test_ddl_db");
  assert (rc != -1);

  (void) create_child_process (ddl_argv,
			       1,
			       NULL,
			       NULL,
			       NULL,
			       &exit_status);
  if (exit_status != 0)
    {
      rc = system ("cubrid server stop test_ddl_db");
      assert (rc != -1);
      std::cout << "Test failed" << std::endl;
      return 1;
    }

  ddl_argv[4] = "insert into t1 values (1); insert into t1 values (2)";
  (void) create_child_process (ddl_argv,
			       1,
			       NULL,
			       NULL,
			       NULL,
			       &exit_status);
  if (exit_status != 0)
    {
      rc = system ("cubrid server stop test_ddl_db");
      assert (rc != -1);
      std::cout << "Test failed" << std::endl;
      return 1;
    }

  rc = system ("cubrid server stop test_ddl_db");
  assert (rc != -1);
  rc = system ("cubrid deletedb test_ddl_db");
  assert (rc != -1);

  std::cout << "Test succeeded" << std::endl;
  return 0;
}
