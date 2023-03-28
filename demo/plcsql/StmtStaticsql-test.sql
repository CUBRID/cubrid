create or replace function StmtStaticSql_test() return int as
    g char(1) := 'M';
    n char(3) := 'KOR';

    c int;
    m varchar;
begin
    select code, name into c, m from athlete where gender = g and nation_code = n;
    return c;

exception
when no_data_found then
    return 0;
when too_many_rows then
    return 100;
when others then
    return -1;
end;
