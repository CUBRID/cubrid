create or replace function StmtForStaticSqlLoop_test() return int as
i int;
begin
    i := 0;
    for r in (select code, name from athlete where gender = 'M' and nation_code = 'KOR') loop
        i := r.code;
    end loop;

    return i;
end;
