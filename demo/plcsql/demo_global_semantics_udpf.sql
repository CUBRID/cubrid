create or replace function demo_global_semantics_udpf() return varchar as
m varchar;
begin
    demo_hello ();
    m := demo_hello_ret ();
    return m;
end;