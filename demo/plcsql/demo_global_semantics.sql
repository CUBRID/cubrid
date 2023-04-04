create or replace function demo_global_semantics() return varchar as
i numeric;
m varchar;
begin
    demo_hello ();
    m := demo_hello_ret ();
    i := demo_pl_serial.NEXT_VALUE;
    return m || ' :' || i;
end;