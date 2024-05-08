/* https://www.cubrid.org/manual/en/11.2/sql/query/insert.html */
/*
    drop table if exists a_tbl1;
    CREATE TABLE a_tbl1(id INT UNIQUE, name VARCHAR, phone VARCHAR DEFAULT '000-0000');
*/
create or replace procedure test_dml_insert() as
    i int;
    n varchar;
    p varchar;

    new_table VARCHAR := 'a_tbl1';
    temp VARCHAR := '';
begin
-- 0)
--insert default values with DEFAULT keyword before VALUES
    INSERT INTO a_tbl1 DEFAULT VALUES;

--insert multiple rows
    INSERT INTO a_tbl1 VALUES (1,'aaa', DEFAULT),(2,'bbb', DEFAULT);

--insert a single row specifying column values for all
    INSERT INTO a_tbl1 VALUES (3,'ccc', '333-3333');

--insert two rows specifying column values for only
    INSERT INTO a_tbl1(id) VALUES (4), (5);

--insert a single row with SET clauses

    temp := temp || '/* ((';
    temp := temp || '(((( */';
    
    temp := temp || 'INSERT INTO a_tbl1 SET id=6, name=''eee'';';

    temp := temp || '/* ((';
    temp := temp || '(((( */';

    DBMS_OUTPUT.put_line (temp);

    EXECUTE IMMEDIATE temp;
    DBMS_OUTPUT.put_line(temp || ' is succeed');

    INSERT INTO a_tbl1 SET id=7, phone='777-7777';

-- 1)
    DBMS_OUTPUT.PUT_LINE('[Test 1] =====================================================================');
    INSERT INTO a_tbl1 SET id=6, phone='000-0000' ON DUPLICATE KEY UPDATE phone='666-6666';

-- 2)
    DBMS_OUTPUT.PUT_LINE('Expected: ');
    DBMS_OUTPUT.PUT_LINE(6 || ' ' || 'eee' || ' ' || '666-6666');

    DBMS_OUTPUT.PUT_LINE('Actual: ');
    for r in (SELECT id, name, phone FROM a_tbl1 WHERE id=6) loop
        i := r.id;
        n := r.name;
        p := r.phone;
        DBMS_OUTPUT.PUT_LINE(i || ' ' || n || ' ' || p);
    end loop;
    DBMS_OUTPUT.PUT_LINE('[Test 1] OK');

-- 3)
    DBMS_OUTPUT.PUT_LINE('[Test 2] =====================================================================');
    DBMS_OUTPUT.PUT_LINE('Expected: ');
    DBMS_OUTPUT.PUT_LINE(7 || ' ' || 'ggg' || ' ' || '777-7777');

    INSERT INTO a_tbl1 SELECT * FROM a_tbl1 WHERE id=7 ON DUPLICATE KEY UPDATE name='ggg';

    DBMS_OUTPUT.PUT_LINE('Actual: ');
    for r in (SELECT id, name, phone FROM a_tbl1 WHERE id=7) loop
        i := r.id;
        n := r.name;
        p := r.phone;
        DBMS_OUTPUT.PUT_LINE(i || ' ' || n || ' ' || p);
    end loop;

    DBMS_OUTPUT.PUT_LINE('[Test 2] OK');
end;
