create or replace function test_query_single_row(c int) return string as
    g char(1) := 'M';
    n char(3) := 'KOR';

    m varchar;
begin
    select name into m from athlete where gender = g and nation_code = n and code = c;
    return m;
exception
when no_data_found then
    return 'no_data_found';
when too_many_rows then
    return 'too_many_rows';
when others then
    return 'others';
end;
