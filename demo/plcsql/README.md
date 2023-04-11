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
plcsql_helper demodb -u public -i $CUBRID/demo/plcsql/demo_hello.sql
```

```
-- test
csql -u public demodb
;server-output on
    call demo_hello ();
;ex
```
---
## 2. Static SQL
### 2.1 Query - Single Rows
-  [test_query_single_row_const.sql](./test_query_single_row_const.sql)
```
-- registration
plcsql_helper demodb -u public -i $CUBRID/demo/plcsql/test_query_single_row_const.sql
```

```
-- test
csql -u public demodb
;server-output on
    select test_query_single_row_const ();
;ex
```

NOTE
- LIMIT clause is not supported yet

-  [test_query_single_row.sql](./test_query_single_row.sql)
```
-- registration
plcsql_helper demodb -u public -i $CUBRID/demo/plcsql/test_query_single_row.sql
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
### 2.2 Query - Cursor
-  [test_query_cursor_simple_nocond.sql](./test_query_cursor_simple_nocond.sql)
-  [test_query_cursor_simple.sql](./test_query_cursor_simple.sql)
```
-- registration
plcsql_helper demodb -u public -i $CUBRID/demo/plcsql/test_query_cursor_simple_nocond.sql
plcsql_helper demodb -u public -i $CUBRID/demo/plcsql/test_query_cursor_simple.sql
```

```
-- test
csql -u public demodb
;server-output on
    select test_query_cursor_simple_nocond ();
    select test_query_cursor_simple ();
;ex
```

-  [test_query_cursor_hostvar.sql](./test_query_cursor_hostvar.sql)
```
-- registration
plcsql_helper demodb -u public -i $CUBRID/demo/plcsql/test_query_cursor_hostvar.sql
```

```
-- test
csql -u public demodb
;server-output on
    select test_query_cursor_hostvar ();
;ex
```
### 2.3 DDL (Dynamic SQL), DML

#### Dynmaic SQL
-  [test_ddl.sql](./test_ddl.sql)
```
-- registration
plcsql_helper demodb -u public -i $CUBRID/demo/plcsql/test_ddl.sql
```

```
-- test
csql -u public demodb
;server-output on
    call test_ddl ();
    desc a_tbl1;
;ex
```

#### INSERT
-  [test_dml_insert.sql](./test_dml_insert.sql)
```
-- preparation
csql -u public demodb
    drop table if exists a_tbl1;
    CREATE TABLE a_tbl1(id INT UNIQUE, name VARCHAR, phone VARCHAR DEFAULT '000-0000');
;ex
```
```
-- registration
plcsql_helper demodb -u public -i $CUBRID/demo/plcsql/test_dml_insert.sql
```

```
-- test
csql -u public demodb
;server-output on
    call test_dml_insert ();
;ex
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
plcsql_helper demodb -u public -i $CUBRID/demo/plcsql/test_dml_delete.sql
```

```
-- test
csql -u public demodb
;server-output on
    call test_dml_delete ();
;ex
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
plcsql_helper demodb -u public -i $CUBRID/demo/plcsql/test_tcl_commit.sql
```

```
-- test
csql -u public demodb
;set pl_transaction_control=yes
;autocommit off
;server-output on
    TRUNCATE test_tcl_tbl;
    SELECT * FROM test_tcl_tbl;
    CALL test_tcl_commit ();
    ROLLBACK; -- rollback in csql session
    SELECT * FROM test_tcl_tbl; -- committed rows should be displayed
;ex
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
plcsql_helper demodb -u public -i $CUBRID/demo/plcsql/test_tcl_rollback.sql
```

```
-- test
csql -u public demodb
;set pl_transaction_control=yes
;set autocommit off
;server-output on
    TRUNCATE test_tcl_tbl2;
    CALL test_tcl_rollback ();
    COMMIT;
    SELECT * FROM test_tcl_tbl2;
;ex
```
---
## 4. Procedure/Function
```
-- registration
plcsql_helper demodb -u public -i $CUBRID/demo/plcsql/demo_hello_ret.sql
plcsql_helper demodb -u public -i $CUBRID/demo/plcsql/demo_global_semantics_udpf.sql
```

```
-- test
csql -u public demodb
;server-output on
    select demo_global_semantics_udpf ();
;ex
```
---
## 5. %TYPE
-- registration
```
plcsql_helper demodb -u public -i $CUBRID/demo/plcsql/demo_global_semantics_type.sql
```

```
-- test
csql -u public demodb
;server-output on
    select demo_global_semantics_type ();
;ex
```
---
## 6. Pseudocolumn

### 6.1 Serial
```
-- preparation
csql -u public demodb
    DROP SERIAL demo_pl_serial;
    CREATE SERIAL demo_pl_serial;
;ex
```

-- registration
```
plcsql_helper demodb -u public -i $CUBRID/demo/plcsql/demo_global_semantics_serial.sql
```

```
-- test
csql -u public demodb
;server-output on
    select demo_global_semantics_serial ();
;ex
```