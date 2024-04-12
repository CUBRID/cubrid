-- all arguments
create or replace procedure demo_default_value (a int := 1) as
begin
    DBMS_OUTPUT.put_line(a);
end;

CALL demo_default_value (3);
CALL demo_default_value ();

create or replace procedure demo_default_value2 (
        a varchar := 'a', 
        b varchar := 'b'
) as
begin
    DBMS_OUTPUT.put_line(a);
    DBMS_OUTPUT.put_line(b);
end;

CALL demo_default_value2 ();
CALL demo_default_value2 ('c');
CALL demo_default_value2 ('c', 'd');

-- trailing arguments
create or replace procedure demo_default_value3 (
        a varchar, 
        b varchar := 'b'
) as
begin
    DBMS_OUTPUT.put_line(a);
    DBMS_OUTPUT.put_line(b);
end;

CALL demo_default_value3 ('k');
CALL demo_default_value3 ('j', 'c');

-- expression
create or replace procedure demo_default_value7 (
        a varchar := TO_CHAR(12345,'S999999')
) as
begin
    DBMS_OUTPUT.put_line(a);
end;

CALL demo_default_value7 ();
CALL demo_default_value7 ('cubrid');

-- null args
create or replace procedure demo_default_value8 (
        a varchar, 
        b varchar := NULL
) as
begin
    DBMS_OUTPUT.put_line(a);
    DBMS_OUTPUT.put_line(b);
end;

CALL demo_default_value8 ('a');
CALL demo_default_value8 ('a', NULL);
CALL demo_default_value8 ('a', 'b');

-- Error 1) no trailing arguments
create or replace procedure demo_default_value4 (
        a varchar := 'a', 
        b varchar
) as
begin
    DBMS_OUTPUT.put_line(a);
    DBMS_OUTPUT.put_line(b);
end;

CALL demo_default_value4 ('c');
CALL demo_default_value4 ('c', 'd');

-- Error) not same type
create or replace procedure demo_default_value5 (
        a varchar, 
        b varchar := 1
) as
begin
    DBMS_OUTPUT.put_line(a);
    DBMS_OUTPUT.put_line(b);
end;

-- Error) type incompatbile
create or replace procedure demo_default_value6 (
        a varchar, 
        b integer := 'a'
) as
begin
    DBMS_OUTPUT.put_line(a);
    DBMS_OUTPUT.put_line(b);
end;

-- Error) out param
create or replace procedure demo_default_value7 (
        a out varchar := 'a'
) as
begin
    DBMS_OUTPUT.put_line(a);
end;

-- Error) out param
create or replace procedure demo_default_value8 (
        a varchar := 'a',
        b out varchar := 'b'
) as
begin
    DBMS_OUTPUT.put_line(a);
end;

-- Error) in out param
create or replace procedure demo_default_value9 (
        a in out varchar:= 'a'
) as
begin
    DBMS_OUTPUT.put_line(a);
end;