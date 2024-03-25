create or replace procedure demo_default_value (a int := 1) as
begin
    DBMS_OUTPUT.put_line(a);
end;

-- all arguments
create or replace procedure demo_default_value2 (
        a varchar := 'a', 
        b varchar := 'b'
) as
begin
    DBMS_OUTPUT.put_line(a);
    DBMS_OUTPUT.put_line(b);
end;

-- trailing arguments
create or replace procedure demo_default_value3 (
        a varchar, 
        b varchar := 'b'
) as
begin
    DBMS_OUTPUT.put_line(a);
    DBMS_OUTPUT.put_line(b);
end;

-- expression
create or replace procedure demo_default_value7 (
        a varchar := TO_CHAR(12345,'S999999')
) as
begin
    DBMS_OUTPUT.put_line(a);
end;


-- Error
create or replace procedure demo_default_value4 (
        a varchar := 'a', 
        b varchar
) as
begin
    DBMS_OUTPUT.put_line(a);
    DBMS_OUTPUT.put_line(b);
end;

create or replace procedure demo_default_value5 (
        a varchar := 1, 
        b varchar
) as
begin
    DBMS_OUTPUT.put_line(a);
    DBMS_OUTPUT.put_line(b);
end;

create or replace procedure demo_default_value6 (
        a varchar := 1, 
        b varchar
) as
begin
    DBMS_OUTPUT.put_line(a);
    DBMS_OUTPUT.put_line(b);
end;
