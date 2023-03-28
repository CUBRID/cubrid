create or replace function StmtStaticSql_test_hostvar() return varchar as
    g char(1) := 'M';
    n char(3) := 'KOR';

    i int;
    m varchar;
begin
    i := 0;
    for r in (select code, name from athlete where gender = g and nation_code = n) loop
        i := r.code;
        m := r.name;
    end loop;

    return m;
exception
when no_data_found then
    return 'no_data';
when too_many_rows then
    return 'too_many';
when others then
    return 'others';
end;
