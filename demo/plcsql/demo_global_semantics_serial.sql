create or replace function demo_global_semantics_serial() return numeric as
i numeric;
begin
    i := demo_pl_serial.NEXT_VALUE;
    return i;
end;