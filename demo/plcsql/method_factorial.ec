#include "dbi.h"
#include "dbtype_def.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
mkdir $CUBRID/method

cubrid_esql -u method_factorial.ec
gcc -c method_factorial.c -I$CUBRID/include -fPIC
gcc -o method_factorial.so method_factorial.o -shared -L$CUBRID/lib -lcubridesql -lm
cp method_factorial.so $CUBRID/method/

csql -u dba demodb
DROP CLASS cub_meth;
CREATE CLASS cub_meth (a int);
ALTER CLASS cub_meth add method class factorial(int) int function exec_factorial file '$CUBRID/method/method_factorial.so', '$CUBRID/lib/libcubridesql.so';

SELECT factorial (CLASS cub_meth, 0);
SELECT factorial (CLASS cub_meth, 1);


create table t1 (i int);
insert into t1 values (0);
SELECT factorial (CLASS cub_meth, i) from t1;
*/

void exec_factorial (DB_OBJECT *obj, DB_VALUE *rtn, DB_VALUE *val)
{
  int n = db_get_int (val);
  int err, iv, res = -1;
  DB_SESSION *session = NULL;
  DB_QUERY_RESULT *result = NULL;
  int stmt_id = -1;
  char buffer [256];
  const char *query = "SELECT factorial (CLASS cub_meth, %d)";
  DB_VALUE next_val;

  if (n == 0)
    {
      res = 1;
    }
  else
    {
      // query
      sprintf (buffer, query, n - 1);
      session = db_open_buffer (buffer);
      if (session == NULL)
	{
	  goto end;
	}
      printf ("open buffer ok\n");

      stmt_id = db_compile_statement (session);
      if (stmt_id < 0)
	{
	  goto end;
	}
      printf ("compile ok\n");

      err = db_execute_statement (session, stmt_id, &result);
      if (err > 0 && result)
	{
	  err = db_query_first_tuple (result);
	  if (err != DB_CURSOR_SUCCESS)
	    {
	      goto end;
	    }

	  printf ("first ok\n");

	  db_make_null (&next_val);
	  err = db_query_get_tuple_value (result, 0, &next_val);
	  if (err == NO_ERROR)
	    {
	      printf ("fetch ok\n");
	      iv = db_get_int (&next_val);
	      if (iv > 0)
		{
		  res = n * iv;
		}
	    }
	  else
	    {
	      printf ("fetch nok: %d\n", err);
	    }
	}
     else
       {
         printf ("execute nok: %d\n", err);
       }
    }

end:
  if (result != NULL)
    {
      db_query_end (result);
    }

  if (session != NULL)
    {
      db_close_session (session);
    }

  db_make_int (rtn, res);
}
