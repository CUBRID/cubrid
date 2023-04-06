/* https://www.cubrid.org/manual/en/11.2/sql/query/insert.html */
/*
    drop table if exists a_tbl1;
    CREATE TABLE a_tbl1(id INT UNIQUE, name VARCHAR, phone VARCHAR DEFAULT '000-0000');
*/
create or replace procedure test_dml() as
    i int;
    n varchar;
    p varchar;

    new_table VARCHAR := 'a_tbl1';
begin
-- 0)
--insert default values with DEFAULT keyword before VALUES
    EXECUTE IMMEDIATE 'INSERT INTO a_tbl1 DEFAULT VALUES';

--insert multiple rows
    EXECUTE IMMEDIATE 'INSERT INTO a_tbl1 VALUES (1,''aaa'', DEFAULT),(2,''bbb'', DEFAULT)';

--insert a single row specifying column values for all
    INSERT INTO a_tbl1 VALUES (3,'ccc', '333-3333');

--insert two rows specifying column values for only
    EXECUTE IMMEDIATE 'INSERT INTO a_tbl1(id) VALUES (4), (5)';

--insert a single row with SET clauses
    EXECUTE IMMEDIATE 'INSERT INTO a_tbl1 SET id=6, name=''eee'';';
    EXECUTE IMMEDIATE 'INSERT INTO a_tbl1 SET id=7, phone=''777-7777'';';

-- 1)
    PUT_LINE('[Test 1] =====================================================================');
    EXECUTE IMMEDIATE 'INSERT INTO a_tbl1 SET id=6, phone=''000-0000'' ON DUPLICATE KEY UPDATE phone=''666-6666'';';

-- 2)
    PUT_LINE('Expected: ');
    PUT_LINE('           id  name                  phone');
    PUT_LINE('=========================================================');
    PUT_LINE('            6  eee                 666-6666');

    PUT_LINE('Actual');
    for r in (SELECT id, name, phone FROM a_tbl1 WHERE id=6) loop
        i := r.id;
        n := r.name;
        p := r.phone;
        PUT_LINE(i);
        PUT_LINE(n || ' ' || p);
    end loop;
    PUT_LINE('[Test 1] OK');

-- 3)
    PUT_LINE('[Test 2] =====================================================================');
    PUT_LINE('Expected: ');
    PUT_LINE('           id  name                  phone
=========================================================
            7  ggg                 777-7777');

    EXECUTE IMMEDIATE 'INSERT INTO a_tbl1 SELECT * FROM a_tbl1 WHERE id=7 ON DUPLICATE KEY UPDATE name=''ggg'';';

    PUT_LINE('Actual');
    for r in (SELECT id, name, phone FROM a_tbl1 WHERE id=7) loop
        i := r.id;
        n := r.name;
        p := r.phone;
        PUT_LINE(i);
        PUT_LINE(n || ' ' || p);
    end loop;

    PUT_LINE('[Test 2] OK');
end;
