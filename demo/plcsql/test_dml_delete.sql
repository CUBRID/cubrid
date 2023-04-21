/* https://www.cubrid.org/manual/en/11.2/sql/query/update.html */
/*
DROP TABLE a_tbl;
CREATE TABLE a_tbl(
    id INT NOT NULL,
    phone VARCHAR(10));
INSERT INTO a_tbl VALUES(1,'111-1111'), (2,'222-2222'), (3, '333-3333'), (4, NULL), (5, NULL);
*/
create or replace procedure test_dml_delete() as
    p varchar;
begin
    DELETE FROM a_tbl WHERE phone IS NULL;

/*
           id  phone
===================================
            1  '111-1111'
            2  '222-2222'
            3  '333-3333'
            5  NULL
*/

    for r in (SELECT phone FROM a_tbl) loop
        p := r.phone;
        PUT_LINE(p);
    end loop;
end;
