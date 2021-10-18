#include "cubrid_esql.h"
#line 1 "test_method.ec"
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
     
	   int a;
	   int b;
	 
	 
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
     
	   int n;
	   int result;
	 
	 
	 uci_startup("t2");
	 
	 n = db_get_int(arg0);
	 
	 if (n == 0 || n == 1) {
	 	db_make_int (rtn, n);
	 }
	 else {
		{ uci_start((void *)&uci_esqlxc_file, __FILE__, __LINE__, 0x0001); 
#line 59 "test_method.ec"
  uci_put_value(NULL, DB_TYPE_INTEGER, 0, 0, DB_TYPE_C_INT, &(n), 0);
#line 59 "test_method.ec"
  uci_put_value(NULL, DB_TYPE_INTEGER, 0, 0, DB_TYPE_C_INT, &(n), 0);
#line 59 "test_method.ec"
  uci_static(-1, "select (m_t2(? - 1) ON CLASS dummy + m_t2(? - 2) ON CLASS dummy) into ? FROM db_root", 84, 1);
#line 59 "test_method.ec"
  uci_get_value(-1, NULL, (void *)(&(result)), DB_TYPE_C_INT, (int)(sizeof(int)), NULL);
#line 59 "test_method.ec"
  uci_end();
#line 59 "test_method.ec"
}

#line 60 "test_method.ec"
		db_make_int (rtn, result);
	 }
}

/*
ALTER CLASS dummy add method class m_t3(int) string function m_t3 file '$CUBRID/method/method.so', '$CUBRID/lib/libcubridesql.so';
SELECT m_t3 (a.event_code) ON CLASS dummy from game a;
*/
void m_t3(DB_OBJECT *obj, DB_VALUE *rtn, DB_VALUE *arg0)
{
     
	   int event_code;
	   struct { int length; char array[50]; } name = { 50, "" }; 	
#line 73 "test_method.ec"
	 

	 uci_startup("t3");

     event_code = db_get_int(arg0);

	 name.length = sizeof(name.array);
	 
	 { uci_start((void *)&uci_esqlxc_file, __FILE__, __LINE__, 0x0001); 
#line 81 "test_method.ec"
  uci_put_value(NULL, DB_TYPE_INTEGER, 0, 0, DB_TYPE_C_INT, &(event_code), 0);
#line 81 "test_method.ec"
  uci_static(-1, "select e.name into ? FROM event e WHERE e.code = ? ", 51, 1);
#line 81 "test_method.ec"
  uci_get_value(-1, NULL, (void *)((name).array), DB_TYPE_C_VARCHAR, (int)(sizeof(name.array)), &(name).length);
#line 81 "test_method.ec"
  uci_end();
#line 81 "test_method.ec"
}

#line 82 "test_method.ec"

     name.length = strlen(name.array);
	 db_make_string (rtn, name.array);
}

/*
ALTER CLASS dummy add method class m_t4(int) string function m_t4 file '$CUBRID/method/method.so', '$CUBRID/lib/libcubridesql.so';
SELECT m_t4 (a.id) ON CLASS dummy from tbl2 a;
*/
void m_t4(DB_OBJECT *obj, DB_VALUE *rtn, DB_VALUE *arg0)
{
     
	   int id;
	   struct { int length; char array[10001]; } name = { 10001, "" }; 	
#line 96 "test_method.ec"
	 

	 uci_startup("t4");

     id = db_get_int(arg0);

	 name.length = sizeof(name.array);
	 
	 { uci_start((void *)&uci_esqlxc_file, __FILE__, __LINE__, 0x0001); 
#line 104 "test_method.ec"
  uci_put_value(NULL, DB_TYPE_INTEGER, 0, 0, DB_TYPE_C_INT, &(id), 0);
#line 104 "test_method.ec"
  uci_static(-1, "select e.str into ? FROM tbl1 e WHERE e.id = ? ", 47, 1);
#line 104 "test_method.ec"
  uci_get_value(-1, NULL, (void *)((name).array), DB_TYPE_C_VARCHAR, (int)(sizeof(name.array)), &(name).length);
#line 104 "test_method.ec"
  uci_end();
#line 104 "test_method.ec"
}

#line 105 "test_method.ec"
     name.length = strlen(name.array);
	 db_make_string (rtn, name.array);
}

void m_t5(DB_OBJECT *obj, DB_VALUE *rtn, DB_VALUE *arg0)
{
     int end_flag = 1;
     int i = 0;
     
       int val;
       struct { int length; char array[4]; } nation_code = { 4, "" }; 
#line 116 "test_method.ec"
	   struct { int length; char array[50]; } name = { 50, "" }; 	
#line 117 "test_method.ec"
	 
	 
#line 119 "test_method.ec"
	 
	 uci_startup("t5");

	 memcpy (nation_code.array, db_get_string(arg0), db_get_string_size(arg0));
	 nation_code.length = db_get_string_size(arg0);

	 
#line 126 "test_method.ec"
	 
	 { uci_start((void *)&uci_esqlxc_file, __FILE__, __LINE__, 0x0001); 
#line 127 "test_method.ec"
  uci_put_value(NULL, DB_TYPE_VARCHAR, sizeof((nation_code).array)-1, 0, DB_TYPE_C_CHAR, (nation_code).array, 0);
#line 127 "test_method.ec"
  uci_open_cs(0, "select e.name FROM athlete e WHERE e.nation_code = ? ", 53, -1, 0);
#line 127 "test_method.ec"
  uci_end();
#line 127 "test_method.ec"
}

#line 128 "test_method.ec"
	 while (end_flag) {
	    name.length = sizeof(name.array); 
	    { uci_start((void *)&uci_esqlxc_file, __FILE__, __LINE__, 0x0001); 
#line 130 "test_method.ec"
  uci_fetch_cs(0, 1);
#line 130 "test_method.ec"
  uci_get_value(0, NULL, (void *)((name).array), DB_TYPE_C_VARCHAR, (int)(sizeof(name.array)), &(name).length);
#line 130 "test_method.ec"
  uci_end();
#line 130 "test_method.ec"
}

#line 131 "test_method.ec"
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
	 
	 { uci_start((void *)&uci_esqlxc_file, __FILE__, __LINE__, 0x0001); 
#line 142 "test_method.ec"
  uci_close_cs(0);
#line 142 "test_method.ec"
  uci_end();
#line 142 "test_method.ec"
}

#line 143 "test_method.ec"
}


/*
ALTER CLASS dummy add method class m_t6(int) string function m_t6 file '$CUBRID/method/method.so', '$CUBRID/lib/libcubridesql.so';
SELECT m_t6 (a.id) ON CLASS dummy from tbl2 a;
*/
void m_t6(DB_OBJECT *obj, DB_VALUE *rtn, DB_VALUE *arg0)
{
     int i = 0;
     int end_flag = 1;
     
       int val;
       struct { int length; char array[10001]; } name = { 10001, "" }; 	
#line 157 "test_method.ec"
	 
	 
#line 159 "test_method.ec"
	 
	 uci_startup("t6");

     val = db_get_int(arg0);
	 
#line 164 "test_method.ec"
	 
	 { uci_start((void *)&uci_esqlxc_file, __FILE__, __LINE__, 0x0001); 
#line 165 "test_method.ec"
  uci_put_value(NULL, DB_TYPE_INTEGER, 0, 0, DB_TYPE_C_INT, &(val), 0);
#line 165 "test_method.ec"
  uci_open_cs(1, "select e.str FROM tbl1 e WHERE e.val = ? ", 41, -1, 0);
#line 165 "test_method.ec"
  uci_end();
#line 165 "test_method.ec"
}

#line 166 "test_method.ec"
	 while (end_flag) {
	    name.length = sizeof(name.array);
	 	{ uci_start((void *)&uci_esqlxc_file, __FILE__, __LINE__, 0x0001); 
#line 168 "test_method.ec"
  uci_fetch_cs(1, 1);
#line 168 "test_method.ec"
  uci_get_value(1, NULL, (void *)((name).array), DB_TYPE_C_VARCHAR, (int)(sizeof(name.array)), &(name).length);
#line 168 "test_method.ec"
  uci_end();
#line 168 "test_method.ec"
}

#line 169 "test_method.ec"
	 	if (i == 0) {
		    name.length = strlen(name.array);
		    db_make_string (rtn, name.array);
	    }
	    i++;
	 	if (sqlca.sqlcode != 0) {
	 	  end_flag = 0;
	 	}
	 }
	 
	 { uci_start((void *)&uci_esqlxc_file, __FILE__, __LINE__, 0x0001); 
#line 179 "test_method.ec"
  uci_close_cs(1);
#line 179 "test_method.ec"
  uci_end();
#line 179 "test_method.ec"
}

#line 180 "test_method.ec"
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

     
       int val;
	 
	 
#line 197 "test_method.ec"
	 
	 uci_startup("t6");

	 
#line 201 "test_method.ec"
	 
	 { uci_start((void *)&uci_esqlxc_file, __FILE__, __LINE__, 0x0001); 
#line 202 "test_method.ec"
  uci_open_cs(2, "select distinct e.event_code FROM game e", 40, -1, 0);
#line 202 "test_method.ec"
  uci_end();
#line 202 "test_method.ec"
}

#line 203 "test_method.ec"
     db_make_null (&v);
     //set_p = db_col_create (DB_TYPE_SET, 400, NULL);
	 set_p = db_set_create_basic (NULL, NULL);
     while (end_flag) {
	 	{ uci_start((void *)&uci_esqlxc_file, __FILE__, __LINE__, 0x0001); 
#line 207 "test_method.ec"
  uci_fetch_cs(2, 1);
#line 207 "test_method.ec"
  uci_get_value(2, NULL, (void *)(&(val)), DB_TYPE_C_INT, (int)(sizeof(int)), NULL);
#line 207 "test_method.ec"
  uci_end();
#line 207 "test_method.ec"
}

#line 208 "test_method.ec"
        printf ("%d\n", val);

        db_make_int (&v, val);
        db_set_add (set_p, db_value_copy(&v));
	    i++;
	 	if (sqlca.sqlcode != 0) {
	 	  end_flag = 0;
	 	}
	 }
	 { uci_start((void *)&uci_esqlxc_file, __FILE__, __LINE__, 0x0001); 
#line 217 "test_method.ec"
  uci_close_cs(2);
#line 217 "test_method.ec"
  uci_end();
#line 217 "test_method.ec"
}

#line 218 "test_method.ec"

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

     
	   int event_code;
	   struct { int length; char array[50]; } name = { 50, "" }; 	
#line 251 "test_method.ec"
	 

	 uci_startup("esqlx_tst");

     event_code = db_get_int(arg0);

	 name.length = sizeof(name.array);
	 
	 { uci_start((void *)&uci_esqlxc_file, __FILE__, __LINE__, 0x0001); 
#line 259 "test_method.ec"
  uci_put_value(NULL, DB_TYPE_INTEGER, 0, 0, DB_TYPE_C_INT, &(event_code), 0);
#line 259 "test_method.ec"
  uci_static(-1, "select e.name into ? FROM event e WHERE e.code = ? ", 51, 1);
#line 259 "test_method.ec"
  uci_get_value(-1, NULL, (void *)((name).array), DB_TYPE_C_VARCHAR, (int)(sizeof(name.array)), &(name).length);
#line 259 "test_method.ec"
  uci_end();
#line 259 "test_method.ec"
}

#line 260 "test_method.ec"

     name.length = strlen(name.array);
	 db_make_string (rtn, name.array);
}

