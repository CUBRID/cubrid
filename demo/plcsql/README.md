# PLCSQL Demonstaration

## Table of Contents

1. [DBMS_OUTPUT](##-1.-DBMS_OUTPUT.put_line)
2. [Static SQL](##-2.-Static-SQL)
3. [TCL](##-3.-TCL-(COMMIT/ROLLBACK))
4. [Procedure/Function](##-4.-Procedure/Function)
5. [%TYPE](##-5.-%TYPE)
6. [Pseudocolumn](##-6.-Pseudocolumn)
---
## 1. DBMS_OUTPUT.put_line
-  [demo_hello.sql](./demo_hello.sql)
```
-- registration
csql -u public demodb -i $CUBRID/demo/plcsql/demo_hello.sql
```

```
-- test
csql -u public demodb
;server-output on
    call demo_hello ();
;ex
```

```
-- expected
Hello CUBRID PL/CSQL!
```
---
## 2. Static SQL
### 2.1 Query - Single Rows
-  [test_query_single_row_const.sql](./test_query_single_row_const.sql)
```
-- registration
csql -u public demodb -i $CUBRID/demo/plcsql/test_query_single_row_const.sql
```

```
-- test
csql -u public demodb
;server-output on
    select test_query_single_row_const ();
;ex
```
```
-- expected
  test_query_single_row_const()
===============================
                          10615
```

-  [test_query_single_row.sql](./test_query_single_row.sql)
```
-- registration
csql -u public demodb -i $CUBRID/demo/plcsql/test_query_single_row.sql
```

```
-- sample
11847  'Kim Yong-Bae'      
11844  'Kim Taek Soo'      
11843  'Kim Tae-Gyun'      
11842  'Kim Soo-Kyung'     
11836  'Kim Moon-Soo'      
11833  'Kim Min-Soo'       
11830  'Kim Kyung-Seok'    
11829  'Kim Kyong-Hun'     
11828  'Kim Ki-Tai'        
11827  'Kim Jung-Chul'     
11825  'Kim Jong-Shin'     
11823  'Kim In-Sub'        
11820  'Kim Han-Soo'       
```
```
-- test
csql -u public demodb
;server-output on
    select test_query_single_row (11828);
;ex
```
```
-- expected
  test_query_single_row(11828)
======================
  'Kim Ki-Tai'    
```
### 2.2 Query - Cursor
-  [test_query_cursor_simple_nocond.sql](./test_query_cursor_simple_nocond.sql)
-  [test_query_cursor_simple.sql](./test_query_cursor_simple.sql)
```
-- registration
csql -u public demodb -i $CUBRID/demo/plcsql/test_query_cursor_simple_nocond.sql
csql -u public demodb -i $CUBRID/demo/plcsql/test_query_cursor_simple.sql
```

```
-- test
csql -u public demodb
;server-output on
    select test_query_cursor_simple_nocond ();
    select test_query_cursor_simple ();
;ex
```
```
-- expected
  test_query_cursor_simple_nocond()
===================================
                              10999

  test_query_cursor_simple()
============================
                       10615
```

-  [test_query_cursor_hostvar.sql](./test_query_cursor_hostvar.sql)
```
-- registration
csql -u public demodb -i $CUBRID/demo/plcsql/test_query_cursor_hostvar.sql
```

```
-- test
csql -u public demodb
;server-output on
    select test_query_cursor_hostvar ();
;ex
```
```
-- expected
  test_query_cursor_hostvar()
======================
  'Han Myung-Woo'     
```
### 2.3 DDL (Dynamic SQL), DML

#### Dynmaic SQL
-  [test_ddl.sql](./test_ddl.sql)
```
-- registration
csql -u public demodb -i $CUBRID/demo/plcsql/test_ddl.sql
```

```
-- test
csql -u public demodb
;server-output on
    call test_ddl ();
    desc a_tbl1;
;ex
```
```
-- expected
creating a_tbl1 table is succeed!

  Field                 Type                  Null                  Key                   Default               Extra               
====================================================================================================================================
  'id'                  'INTEGER'             'YES'                 'UNI'                 NULL                  ''                  
  'name'                'VARCHAR(1073741823)'  'YES'                 ''                    NULL                  ''                  
  'phone'               'VARCHAR(1073741823)'  'YES'                 ''                    '000-0000'            ''                  
```

#### INSERT
-  [test_dml_insert.sql](./test_dml_insert.sql)
-  [test_dml_truncate.sql](./test_dml_truncate.sql)
```
-- preparation and registration
csql -u public demodb
    drop table if exists a_tbl1;
    CREATE TABLE a_tbl1(id INT UNIQUE, name VARCHAR, phone VARCHAR DEFAULT '000-0000');
;ex

csql -u public demodb -i $CUBRID/demo/plcsql/test_dml_insert.sql
csql -u public demodb -i $CUBRID/demo/plcsql/test_dml_truncate.sql
```

```
-- test
csql -u public demodb
;server-output on
    call test_dml_insert ();
;ex
```

```
-- expected
/* (((((( */INSERT INTO a_tbl1 SET id=6, name='eee';/* (((((( */
/* (((((( */INSERT INTO a_tbl1 SET id=6, name='eee';/* (((((( */ is succeed
[Test 1] =====================================================================
Expected: 
6 eee 666-6666
Actual: 
6 eee 666-6666
[Test 1] OK
[Test 2] =====================================================================
Expected: 
7 ggg 777-7777
Actual: 
7 ggg 777-7777
[Test 2] OK
```

#### TRUNCATE
```
-- test
csql -u public demodb
;server-output on
    call test_dml_truncate ();
    SELECT * FROM a_tbl1;
;ex
```

```
-- expected
There are no results.
```

#### DELETE
-  [test_dml_delete.sql](./test_dml_delete.sql)
```
-- preparation
csql -u public demodb
    DROP TABLE a_tbl;
    CREATE TABLE a_tbl(
        id INT NOT NULL,
        phone VARCHAR(10));
    INSERT INTO a_tbl VALUES(1,'111-1111'), (2,'222-2222'), (3, '333-3333'), (4, NULL), (5, NULL);
;ex
```
```
-- registration
csql -u public demodb -i $CUBRID/demo/plcsql/test_dml_delete.sql
```

```
-- test
csql -u public demodb
;server-output on
    call test_dml_delete ();
;ex
```
```
-- expected
111-1111
222-2222
333-3333
```

## 3. TCL (COMMIT/ROLLBACK)

### COMMIT
-  [test_tcl_commit.sql](./test_tcl_commit.sql)
```
-- preparation
csql -u public demodb
    DROP TABLE IF EXISTS test_tcl_tbl;
    CREATE TABLE test_tcl_tbl (code INT, name STRING);
;ex
```
```
-- registration
csql -u public demodb -i $CUBRID/demo/plcsql/test_tcl_commit.sql
```

```
-- test (;set pl_transaction_control=yes)
csql -u public demodb
;set pl_transaction_control=yes
;autocommit off
;server-output on
    TRUNCATE test_tcl_tbl;
    COMMIT;
    SELECT * FROM test_tcl_tbl;
    CALL test_tcl_commit ();
    ROLLBACK; -- rollback in csql session

    SELECT * FROM test_tcl_tbl; -- committed rows should be displayed
    ROLLBACK;
;ex
```
```
-- expected

// CALL test_tcl_commit ();
code = 3, name = ccc
code = 4, name = ddd

// SELECT * FROM test_tcl_tbl;
         code  name                
===================================
            1  'aaa'               
            2  'bbb'               
            3  'ccc'               
            4  'ddd'               
```

```
-- test (;set pl_transaction_control=no)
csql -u public demodb
;set pl_transaction_control=no
;autocommit off
;server-output on
    TRUNCATE test_tcl_tbl;
    COMMIT; -- COMMIT is required to ensure TRUNCATE is executed according to the TRUNCATE spec.

    SELECT * FROM test_tcl_tbl;
    CALL test_tcl_commit ();
    ROLLBACK; -- rollback in csql session
    SELECT * FROM test_tcl_tbl; -- COMMIT in test_tcl_commit () must be ignored
    ROLLBACK;
;ex
```

```
-- expected

// CALL test_tcl_commit ();
code = 3, name = ccc
code = 4, name = ddd

// SELECT * FROM test_tcl_tbl;
There are no results.             
```

### ROLLBACK
-  [test_tcl_rollback.sql](./test_tcl_rollback.sql)
```
-- preparation
csql -u public demodb
    DROP TABLE IF EXISTS test_tcl_tbl2;
    CREATE TABLE test_tcl_tbl2 (code INT, name STRING);
;ex
```
```
-- registration
csql -u public demodb -i $CUBRID/demo/plcsql/test_tcl_rollback.sql
```

```
-- test (;set pl_transaction_control=yes)
csql -u public demodb
;set pl_transaction_control=yes
;set autocommit off
;server-output on
    TRUNCATE test_tcl_tbl2;
    COMMIT;
    CALL test_tcl_rollback ();
    COMMIT;
    SELECT * FROM test_tcl_tbl2;
;ex
```

```
-- expected
         code  name                
===================================
            1  'aaa'               
            2  'bbb'               
            3  'ccc'               
            4  'ddd'                      
```

```
-- test (;set pl_transaction_control=no)
csql -u public demodb
;set pl_transaction_control=no
;set autocommit off
;server-output on
    TRUNCATE test_tcl_tbl2;
    COMMIT;
    CALL test_tcl_rollback ();
    COMMIT;
    SELECT * FROM test_tcl_tbl2;
;ex
```

```
-- expected
         code  name                
===================================
            1  'aaa'               
            2  'bbb'               
            3  'ccc'               
            4  'ddd'               
            6  'daf'               
            7  'qwe'                               
```

---
## 4. Procedure/Function
-  [demo_hello_ret.sql](./demo_hello_ret.sql)
-  [demo_global_semantics_udpf.sql](./demo_global_semantics_udpf.sql)
```
-- registration
csql -u public demodb -i $CUBRID/demo/plcsql/demo_hello_ret.sql
csql -u public demodb -i $CUBRID/demo/plcsql/demo_global_semantics_udpf.sql
```

```
-- test
csql -u public demodb
;server-output on
    select demo_global_semantics_udpf ();
;ex
```

```
-- expected
  demo_global_semantics_udpf()
======================
  'hello cubrid'      

Hello CUBRID PL/CSQL!
```
---
## 5. %TYPE
-  [demo_global_semantics_type.sql](./demo_global_semantics_type.sql)
-- registration
```
csql -u public demodb -i $CUBRID/demo/plcsql/demo_global_semantics_type.sql
```

```
-- test
csql -u public demodb
;server-output on
    select demo_global_semantics_type ();
;ex
```
```
-- expected
  demo_global_semantics_type()
======================
  'Chung Min-Tae'     
```
---
## 6. Pseudocolumn

### 6.1 Serial
-  [demo_global_semantics_serial.sql](./demo_global_semantics_serial.sql)
```
-- preparation
csql -u public demodb
    DROP SERIAL demo_pl_serial;
    CREATE SERIAL demo_pl_serial;
;ex
```

-- registration
```
csql -u public demodb -i $CUBRID/demo/plcsql/demo_global_semantics_serial.sql
```

```
-- test
csql -u public demodb
;server-output on
    select demo_global_semantics_serial ();
    select demo_global_semantics_serial ();
    select demo_global_semantics_serial ();
;ex
```

```
-- expected
  demo_global_semantics_serial()
======================
  1                   

  demo_global_semantics_serial()
======================
  2                   

  demo_global_semantics_serial()
======================
  3                   
```
