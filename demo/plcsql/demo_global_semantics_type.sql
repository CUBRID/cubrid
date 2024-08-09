create or replace function demo_global_semantics_type() return string as
v_name athlete.name%TYPE;
g char(1) := 'M';
n char(3) := 'KOR';

begin
    for r in (select name from athlete where gender = g and nation_code = n) loop
        v_name := r.name;
        EXIT;
    end loop;
    return v_name;
end;