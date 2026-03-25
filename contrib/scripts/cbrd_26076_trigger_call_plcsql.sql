-- CBRD-26076
-- Verify trigger ACTION can execute CALL for a PL/CSQL stored procedure.

create table cbrd26076_src
(
  id int,
  msg string
);

create table cbrd26076_log
(
  src_id int,
  note string
);

create or replace procedure cbrd26076_log_proc (p_src_id int, p_note string)
as language plcsql
begin
  insert into cbrd26076_log values (p_src_id, p_note);
end;

create trigger cbrd26076_tr_call_sp
before insert on cbrd26076_src
execute call cbrd26076_log_proc (new.id, 'trigger call ok[' || new.id || ']');

insert into cbrd26076_src values (1, 'a');
insert into cbrd26076_src values (2, 'b');

select * from cbrd26076_log;

-- Expected:
--   row_count = 2
--   first_src_id = 999
--   first_note = 'trigger call ok'
select count(*) as row_count, min(src_id) as first_src_id, min(note) as first_note
from cbrd26076_log;

drop trigger cbrd26076_tr_call_sp;
drop procedure cbrd26076_log_proc;
drop table cbrd26076_log;
drop table cbrd26076_src;
