/*
DROP TABLE IF EXISTS test_tcl_tbl2;
CREATE TABLE test_tcl_tbl2 (code INT, name STRING);

;set autocommit off
;server-output on
TRUNCATE test_tcl_tbl2;
CALL test_tcl_rollbackk ();

COMMIT;
SELECT * FROM test_tcl_tbl2;
*/

create or replace procedure test_tcl_rollback () as
    i int;
    n varchar;
begin
    INSERT INTO test_tcl_tbl2 VALUES (1,'aaa');
    INSERT INTO test_tcl_tbl2 VALUES (2,'bbb');
    INSERT INTO test_tcl_tbl2 VALUES (3,'ccc');
    INSERT INTO test_tcl_tbl2 VALUES (4,'ddd');
    COMMIT;

    INSERT INTO test_tcl_tbl2 VALUES (6,'daf');
    INSERT INTO test_tcl_tbl2 VALUES (7,'qwe');

    ROLLBACK;

    for r in (SELECT code, name FROM test_tcl_tbl2 WHERE code > 2) loop
        i := r.code;
        n := r.name;
        PUT_LINE(i);
        PUT_LINE('name = ' || n);
    end loop;
end;