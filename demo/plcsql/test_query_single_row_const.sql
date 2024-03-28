create or replace function test_query_single_row_const() return int as
    c int;
    m varchar;
begin
    select code, name INTO c, m from athlete where gender = 'M' and nation_code = 'KOR' LIMIT 1;
    return c;
exception
when no_data_found then
    return 0;
when too_many_rows then
    return 100;
when others then
    return -1;
end;
