create or replace function StmtStaticSql_test() return int as
begin
    select code, name from athlete where gender = 'M' and nation_code = 'KOR';
    return 1;
exception
when no_data_found then
    return 0;
when too_many_rows then
    return 100;
when others then
    return -1;
end;
