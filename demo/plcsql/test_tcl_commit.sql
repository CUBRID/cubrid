/*
csql -u public demodb
DROP TABLE IF EXISTS test_tcl_tbl;
CREATE TABLE test_tcl_tbl (code INT, name STRING);

csql -u public demodb
;autocommit off
;server-output on
TRUNCATE test_tcl_tbl;
CALL test_tcl_commit ();
ROLLBACK; -- rollback in csql session
SELECT * FROM test_tcl_tbl; -- committed rows should be displayed
*/
create or replace procedure test_tcl_commit() as
    i int;
    n varchar;
begin
    INSERT INTO test_tcl_tbl VALUES (1,'aaa');
    INSERT INTO test_tcl_tbl VALUES (2,'bbb');
    INSERT INTO test_tcl_tbl VALUES (3,'ccc');
    INSERT INTO test_tcl_tbl VALUES (4,'ddd');
    COMMIT;

    for r in (SELECT code, name FROM test_tcl_tbl WHERE code > 2) loop
        i := r.code;
        n := r.name;
        PUT_LINE(i);
        PUT_LINE('name = ' || n);
    end loop;
end;