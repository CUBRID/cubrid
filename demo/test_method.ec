#include "dbi.h"

#include <stdio.h>

/*
cubrid_esql -u method.ec
gcc -c method.c -I$CUBRID/include -fPIC
gcc -o method.so method.o -shared -L$CUBRID/lib -lcubridesql -lm
cp method.so $CUBRID/method
*/

void m_t0(DB_OBJECT *obj, DB_VALUE *rtn, DB_VALUE *arg0)
{
	 uci_startup("t0");

	 db_make_int (rtn, db_get_int(arg0));
}

/*
* alter class history add method class m_t1(int,int) int function m_t1 file '$CUBRID/method/method.so', '$CUBRID/lib/libcubridesql.so'
SELECT m_t1(event_code, athlete_code) on class history from game;
*/
void m_t1(DB_OBJECT *obj, DB_VALUE *rtn, DB_VALUE *arg0, DB_VALUE *arg1)
{
     EXEC SQLX BEGIN DECLARE SECTION;
	   int a;
	   int b;
	 EXEC SQLX END DECLARE SECTION;
	 
	 uci_startup("t1");
	 
	 a = db_get_int(arg0);
	 b = db_get_int(arg1);
	 
	 db_make_int (rtn, a + b);
}

/*
DROP CLASS dummy;
CREATE CLASS dummy (a int);
ALTER CLASS dummy add method class m_t2(int) int function m_t2 file '$CUBRID/method/method.so', '$CUBRID/lib/libcubridesql.so';
SELECT m_t2 (5) ON CLASS dummy;
*/
void m_t2(DB_OBJECT *obj, DB_VALUE *rtn, DB_VALUE *arg0)
{
     EXEC SQLX BEGIN DECLARE SECTION;
	   int n;
	   int result;
	 EXEC SQLX END DECLARE SECTION;
	 
	 uci_startup("t2");
	 
	 n = db_get_int(arg0);
	 
	 if (n == 0 || n == 1) {
	 	db_make_int (rtn, n);
	 }
	 else {
		EXEC SQLX select (m_t2(:n - 1) ON CLASS dummy + m_t2(:n - 2) ON CLASS dummy) into :result FROM db_root;
		db_make_int (rtn, result);
	 }
}

/*
ALTER CLASS dummy add method class m_t3(int) string function m_t3 file '$CUBRID/method/method.so', '$CUBRID/lib/libcubridesql.so';
SELECT m_t3 (a.event_code) ON CLASS dummy from game a;
*/
void m_t3(DB_OBJECT *obj, DB_VALUE *rtn, DB_VALUE *arg0)
{
     EXEC SQLX BEGIN DECLARE SECTION;
	   int event_code;
	   VARCHAR name[50];	
	 EXEC SQLX END DECLARE SECTION;

	 uci_startup("t3");

     event_code = db_get_int(arg0);

	 name.length = sizeof(name.array);
	 
	 EXEC SQLX select e.name into :name FROM event e WHERE e.code = :event_code;

     name.length = strlen(name.array);
	 db_make_string (rtn, name.array);
}

/*
ALTER CLASS dummy add method class m_t4(int) string function m_t4 file '$CUBRID/method/method.so', '$CUBRID/lib/libcubridesql.so';
SELECT m_t4 (a.id) ON CLASS dummy from tbl2 a;
*/
void m_t4(DB_OBJECT *obj, DB_VALUE *rtn, DB_VALUE *arg0)
{
     EXEC SQLX BEGIN DECLARE SECTION;
	   int id;
	   VARCHAR name[10001];	
	 EXEC SQLX END DECLARE SECTION;

	 uci_startup("t4");

     id = db_get_int(arg0);

	 name.length = sizeof(name.array);
	 
	 EXEC SQLX select e.str into :name FROM tbl1 e WHERE e.id = :id;
     name.length = strlen(name.array);
	 db_make_string (rtn, name.array);
}

void m_t5(DB_OBJECT *obj, DB_VALUE *rtn, DB_VALUE *arg0)
{
     int end_flag = 1;
     int i = 0;
     EXEC SQLX BEGIN DECLARE SECTION;
       int val;
       VARCHAR nation_code[4];
	   VARCHAR name[50];	
	 EXEC SQLX END DECLARE SECTION;
	 EXEC SQLX INCLUDE SQLCA;
	 
	 uci_startup("t5");

	 memcpy (nation_code.array, db_get_string(arg0), db_get_string_size(arg0));
	 nation_code.length = db_get_string_size(arg0);

	 EXEC SQLX DECLARE c1 CURSOR FOR select e.name FROM athlete e WHERE e.nation_code = :nation_code;
	 
	 EXEC SQLX OPEN c1;
	 while (end_flag) {
	    name.length = sizeof(name.array); 
	    EXEC SQLX fetch c1 into :name;
	    if (i == 0)
	    {
		    name.length = strlen(name.array);
		    db_make_string (rtn, name.array);
	    }
	    i++;
	 	if (sqlca.sqlcode != 0) {
	 		end_flag = 0;
	 	}
	 }
	 
	 EXEC SQLX CLOSE c1;
}


/*
ALTER CLASS dummy add method class m_t6(int) string function m_t6 file '$CUBRID/method/method.so', '$CUBRID/lib/libcubridesql.so';
SELECT m_t6 (a.id) ON CLASS dummy from tbl2 a;
*/
void m_t6(DB_OBJECT *obj, DB_VALUE *rtn, DB_VALUE *arg0)
{
     int i = 0;
     int end_flag = 1;
     EXEC SQLX BEGIN DECLARE SECTION;
       int val;
       VARCHAR name[10001];	
	 EXEC SQLX END DECLARE SECTION;
	 EXEC SQLX INCLUDE SQLCA;
	 
	 uci_startup("t6");

     val = db_get_int(arg0);
	 EXEC SQLX DECLARE c1 CURSOR FOR select e.str FROM tbl1 e WHERE e.val = :val;
	 
	 EXEC SQLX OPEN c1;
	 while (end_flag) {
	    name.length = sizeof(name.array);
	 	EXEC SQLX fetch c1 into :name;
	 	if (i == 0) {
		    name.length = strlen(name.array);
		    db_make_string (rtn, name.array);
	    }
	    i++;
	 	if (sqlca.sqlcode != 0) {
	 	  end_flag = 0;
	 	}
	 }
	 
	 EXEC SQLX CLOSE c1;
}

/*
ALTER CLASS dummy add method class m_t6(int) string function m_t6 file '$CUBRID/method/method.so', '$CUBRID/lib/libcubridesql.so';
SELECT m_t7 () ON CLASS dummy from tbl2 a;
*/
void m_t7(DB_OBJECT *obj, DB_VALUE *rtn)
{
     int i = 0;
     int end_flag = 1;
     DB_COLLECTION *set_p;
	 DB_VALUE v;

     EXEC SQLX BEGIN DECLARE SECTION;
       int val;
	 EXEC SQLX END DECLARE SECTION;
	 EXEC SQLX INCLUDE SQLCA;
	 
	 uci_startup("t6");

	 EXEC SQLX DECLARE c1 CURSOR FOR select distinct e.event_code FROM game e;
	 
	 EXEC SQLX OPEN c1;
     db_make_null (&v);
     //set_p = db_col_create (DB_TYPE_SET, 400, NULL);
	 set_p = db_set_create_basic (NULL, NULL);
     while (end_flag) {
	 	EXEC SQLX fetch c1 into :val;
        printf ("%d\n", val);

        db_make_int (&v, val);
        db_set_add (set_p, db_value_copy(&v));
	    i++;
	 	if (sqlca.sqlcode != 0) {
	 	  end_flag = 0;
	 	}
	 }
	 EXEC SQLX CLOSE c1;

     db_make_set (rtn, set_p);
}

/*
ALTER CLASS dummy add method class m_t6(int) string function m_t6 file '$CUBRID/method/method.so', '$CUBRID/lib/libcubridesql.so';
SELECT m_t6 (a.id) ON CLASS dummy from tbl2 a;
*/
void m_broker_stat(DB_OBJECT *obj, DB_VALUE *rtn, DB_VALUE *arg0)
{
  const char* command = "cubrid broker status -b -f -t -c";
  int status, n_read;
  char buffer [10000] = {0};
  FILE* pipe = popen(command, "r");
  if (!pipe) {
     db_make_string (rtn, "");
     return;
  }
  
  while (!feof(pipe)) {
      n_read = fread(buffer, 1, 10000, pipe);
  }
  
  status = pclose(pipe);
  db_make_string (rtn, buffer);
}

void m_sq(DB_OBJECT *obj, DB_VALUE *rtn, DB_VALUE *arg0)
{

     EXEC SQLX BEGIN DECLARE SECTION;
	   int event_code;
	   VARCHAR name[50];	
	 EXEC SQLX END DECLARE SECTION;

	 uci_startup("esqlx_tst");

     event_code = db_get_int(arg0);

	 name.length = sizeof(name.array);
	 
	 EXEC SQLX select e.name into :name FROM event e WHERE e.code = :event_code;

     name.length = strlen(name.array);
	 db_make_string (rtn, name.array);
}

