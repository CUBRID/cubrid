create or replace procedure test_ddl() as
    i int;
    n varchar;
    p varchar;

    new_table VARCHAR := 'a_tbl1';
begin
    EXECUTE IMMEDIATE 'drop table if exists ' || new_table;
    EXECUTE IMMEDIATE 'CREATE TABLE ' || new_table || ' (id INT UNIQUE, name VARCHAR, phone VARCHAR DEFAULT ''000-0000'');';
    put_line ('creating ' || new_table || ' table is succeed!');
end;