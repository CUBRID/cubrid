/*
create or replace procedure demo_0_hello() as
language java name 'Proc_DEMO_0_HELLO.$DEMO_0_HELLO()';

call demo_0_hello();
 */
create or replace procedure demo_0_hello() as
begin
    put_line('Hello world');
end;
