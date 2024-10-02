-- Related issues
-- http://jira.cubrid.com/browse/OFFICE-153
-- http://jira.cubrid.com/browse/RND-1582
-- http://jira.cubrid.com/browse/RND-390

;plan detail
;trace on

create or replace function demo_concat (a string, b string) return string as 
begin
        return a || b;
end;

create or replace function demo_sp_plus (a int, b int) return int as 
begin
    return a + b;
end;

create or replace function demo_idx_scan_str (a varchar) return varchar as 
begin
    return a;
end;

create or replace function demo_idx_scan_int (a int) return int as 
begin
    return a;
end;

create or replace procedure demo_out_param (a IN VARCHAR, b OUT VARCHAR) as 
begin
    b := a || ' cubrid';
end;

create or replace procedure demo_in_out_param (a IN OUT VARCHAR) as 
begin
    a := a || ' cubrid';
end;


-- normal case
select demo_idx_scan_str (host_year) from record limit 10;

-- out parameter
select '' into :a;

call demo_out_param ('cubrid', :a);
select :a;

call demo_in_out_param (:a);
select :a;

select '' into :b;

select demo_out_param ('cubrid', :b);
select :b;

select demo_in_out_param (:b);
select :b;

-- regular variable chains
select demo_sp_plus (demo_sp_plus (host_year, host_year), host_year) from record limit 10;
select demo_sp_plus (demo_sp_plus (host_year, event_code), host_year + event_code) from record limit 10;
select demo_sp_plus (0, host_year + event_code), demo_sp_plus (host_year, event_code) from record limit 10;

-- orderby skip

select /*+ recompile */ 
        host_year, score, host_year
from record 
order by host_year, event_code
limit 10;

select /*+ recompile */ 
        host_year, score, demo_idx_scan_int (host_year) 
from record 
order by host_year, event_code
limit 10;

/*
Actual:  Index scan(...)
        Sort(order by)
Expected: Index scan(...) 
        skip ORDER BY
*/

-- http://jira.cubrid.com/browse/RND-2108

SELECT DISTINCT h.host_year, o.host_year, o.host_nation
FROM olympic o LEFT OUTER JOIN history h ON h.host_year = o.host_year
WHERE o.host_year > 1950;

SELECT DISTINCT demo_idx_scan_str(h.host_year), demo_idx_scan_str(o.host_year), o.host_nation
FROM olympic o LEFT OUTER JOIN history h ON h.host_year = o.host_year
WHERE o.host_year > 1950;

SELECT DISTINCT h.host_year, o.host_year, o.host_nation
FROM history h, olympic o
WHERE o.host_year = h.host_year(+) AND o.host_year > 1950;

SELECT DISTINCT demo_idx_scan_str(h.host_year), demo_idx_scan_str(o.host_year), o.host_nation
FROM history h, olympic o
WHERE o.host_year = h.host_year(+) AND o.host_year > 1950;

drop if exists tmp;
create table tmp (col1 int, col2 int);
prepare stmt4 from 'select demo_idx_scan_int(a.col1) from tmp a, tmp b where a.col2 = b.col2(+) and b.col1 = ?';
execute stmt4 using 1;

-- order by

SELECT /*+ recompile */
 CONCAT ( host_year, 'M' ) AS host_year
FROM game
WHERE 1=1
ORDER BY game_date
LIMIT 10;

SELECT /*+ recompile */
 DEMO_CONCAT ( host_year, 'M' ) AS host_year
FROM game
WHERE 1=1
ORDER BY game_date
LIMIT 5;

SELECT /*+ recompile */
 game_date, DEMO_CONCAT ( host_year, 'M' ) AS host_year
FROM game
WHERE 1=1
ORDER BY 1
LIMIT 10;

create or replace function increment (a int) return int as 
begin
    return a + 1;
end;


SELECT athlete_code + 1 FROM game LIMIT 3;
SELECT increment (athlete_code) FROM game LIMIT 4;





SELECT /*+ recompile */
 DEMO_CONCAT ( host_year, '-TEST' ) AS host_year
FROM game
ORDER BY game_date 
LIMIT 5;

