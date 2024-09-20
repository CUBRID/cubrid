create or replace function test_factorial(n int) return int as
i int := 0;
res int;
begin
    if n = 0 then
        res := 1;
    else
        SELECT test_factorial(n - 1) INTO i;
        res := n * i;
    end if;
    DBMS_OUTPUT.put_line (res);
    return res;
end;

select test_factorial (i) from t1;
