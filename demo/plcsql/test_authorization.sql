csql -u dba demodb --no-pager
  SET SYSTEM PARAMETERS 'print_object_as_oid=yes';
  CREATE OR REPLACE FUNCTION sp1() return varchar as begin return 'hello'; end;
  CREATE USER t1;
  SELECT grantor.name, grantee.name, class_of FROM _db_auth WHERE object_type = 5; 
  SELECT _db_stored_procedure.identity, sp_name FROM _db_stored_procedure;
;ex

csql -u t1 demodb
  SELECT sp1 (); // expected: EXECUTE authorization failure.
;ex

csql -u dba demodb --no-pager
  SET SYSTEM PARAMETERS 'print_object_as_oid=yes';
  GRANT EXECUTE ON FUNCTION sp1 TO t1;
  SELECT grantor.name, grantee.name, class_of FROM _db_auth WHERE object_type = 5; 
;ex

csql -u t1 demodb
 SELECT sp1 (); // expected: 'hello' -- 미구현, owner만 가능
;ex

csql -u dba demodb --no-pager
  SET SYSTEM PARAMETERS 'print_object_as_oid=yes';
  REVOKE EXECUTE ON FUNCTION sp1 FROM t1;
  SELECT grantor.name, grantee.name, class_of FROM _db_auth WHERE object_type = 5; 
;ex

csql -u t1 demodb
 SELECT sp1 (); // expected: EXECUTE authorization failure.
;ex

csql -u dba demodb
  SET SYSTEM PARAMETERS 'print_object_as_oid=yes';
  CREATE OR REPLACE FUNCTION sp1() return varchar as begin return 'hello'; end;
  GRANT EXECUTE ON FUNCTION sp1 TO t1;
  SELECT grantor.name, grantee.name, class_of FROM _db_auth WHERE object_type = 5; 

  DROP FUNCTION sp1;
  SELECT grantor.name, grantee.name, class_of FROM _db_auth WHERE object_type = 5; 
;ex

csql -u t1 demodb
 SELECT sp1 (); // expected: Function sp1 is undefined.
;ex

csql -u dba demodb --no-pager
  CREATE USER t1;
  GRANT SELECT ON olympic TO t1;

  SET SYSTEM PARAMETERS 'print_object_as_oid=yes';
  SELECT grantor.name, grantee.name, class_of FROM _db_auth;
  SELECT owner.name, grants FROM db_authorization;

  call drop_user('t1') on class db_user;
  SELECT grantor.name, grantee.name, class_of FROM _db_auth;
  SELECT owner.name, grants FROM db_authorization;
;ex

csql -u dba demodb --no-pager
        call login('dba') on class db_user;
        call add_user('user1') on class db_user;
        grant select on db_class to user1;
        call login('user1') on class db_user;
        select * from db_class order by 1;
        call login('dba') on class db_user;
        call drop_user('user1') on class db_user;
;ex