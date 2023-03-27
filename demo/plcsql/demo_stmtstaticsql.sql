create or replace function StmtForStaticSqlLoop_test_without_condition() return int as
i int;
begin
    i := 0;
    for r in (select code, name from athlete) loop
        i := r.code;
        EXIT;
    end loop;

    return i;
end;