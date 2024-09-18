create or replace function test_factorial(n int) return int as
i int := 0;
begin
    if n = 0 then
        return 1;
    else
        SELECT test_factorial(n - 1) INTO i;
        return n * i;
    end if;
end;
