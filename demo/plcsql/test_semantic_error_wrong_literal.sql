/*
    create table test_insert(t time);
*/
create or replace procedure test_wrong_literal() as
begin
    insert into test_insert(t) values (time'12:13:14.123'); -- wrong time literal
end;
