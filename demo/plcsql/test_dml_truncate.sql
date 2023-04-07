/* https://www.cubrid.org/manual/en/11.2/sql/query/update.html */
/*

*/
create or replace procedure test_dml_truncate() as
begin
    truncate TABLE a_tbl1;
end;
