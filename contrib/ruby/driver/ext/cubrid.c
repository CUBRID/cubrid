/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
 *
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer. 
 *
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution. 
 *
 * - Neither the name of the <ORGANIZATION> nor the names of its contributors 
 *   may be used to endorse or promote products derived from this software without 
 *   specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE. 
 *
 */

#include "cubrid.h"

char *cci_client_name = "CCI"; /* for lower than 7.0 */
VALUE cCubrid, cConnection, cStatement, cOid;
extern VALUE cubrid_conn_new(char *host, int port, char *db, char *user, char *passwd);

/* call-seq:
 *   connect(db, host, port, user, password) -> Connection
 *
 * 데이터베이스 서버에 연결하고 Connection 객체를 반환합니다.
 * db는 반드시 주어져야 하고, host, port, user, password는 옵션입니다. 
 * 이들의 디폴트 값은 각각 'localhost', 30000, 'PUBLIC', '' 입니다.
 *
 *  con = Cubrid.connect('demodb', '192.168.1.1', '33000', 'foo','bar')
 *  con.to_s  #=>  host: 192.168.1.1, port: 33000, db: demodb, user: foo
 *
 *  con = Cubrid.connect('subway')
 *  con.to_s  #=>  host: localhost, port: 30000, db: subway, user: PUBLIC
 *
 *  con = Cubrid.connect('subway', '192.168.1.2')
 *  con.to_s  #=>  host: 192.168.1.2, port: 33000, db: subway, user: PUBLIC
 */
VALUE cubrid_connect(int argc, VALUE* argv, VALUE self)
{
  VALUE host, port, db, user, passwd;

  rb_scan_args(argc, argv, "14", &db, &host, &port, &user, &passwd);
  
  if (NIL_P(db))
    rb_raise(rb_eStandardError, "DB name is required.");

  if (NIL_P(host))
    host = rb_str_new2("localhost");
    
  if (NIL_P(port))
    port = INT2NUM(30000);
    
  if (NIL_P(user))
    user = rb_str_new2("PUBLIC");

  if (NIL_P(passwd))
    passwd = rb_str_new2("");

  return cubrid_conn_new(StringValueCStr(host), NUM2INT(port), 
                         StringValueCStr(db), StringValueCStr(user), StringValueCStr(passwd));
}

/* from conn.c */
extern VALUE cubrid_conn_close(VALUE self);
extern VALUE cubrid_conn_prepare(int argc, VALUE* argv, VALUE self);
extern VALUE cubrid_conn_query(int argc, VALUE* argv, VALUE self);
extern VALUE cubrid_conn_commit(VALUE self);
extern VALUE cubrid_conn_rollback(VALUE self);
extern VALUE cubrid_conn_get_auto_commit(VALUE self);
extern VALUE cubrid_conn_set_auto_commit(VALUE self, VALUE auto_commit);
extern VALUE cubrid_conn_to_s(VALUE self);
extern VALUE cubrid_conn_glo_new(VALUE self, VALUE table, VALUE file);
extern VALUE cubrid_conn_server_version(VALUE self);

/* from stmt.c */
extern VALUE cubrid_stmt_close(VALUE self);
extern VALUE cubrid_stmt_bind(int argc, VALUE* argv, VALUE self);
extern VALUE cubrid_stmt_execute(int argc, VALUE* argv, VALUE self);
extern VALUE cubrid_stmt_affected_rows(VALUE self);
extern VALUE cubrid_stmt_fetch(VALUE self);
extern VALUE cubrid_stmt_fetch_hash(VALUE self);
extern VALUE cubrid_stmt_each(VALUE self);
extern VALUE cubrid_stmt_each_hash(VALUE self);
extern VALUE cubrid_stmt_column_info(VALUE self);
extern VALUE cubrid_stmt_get_oid(VALUE self);

/* from oid.c */
extern VALUE cubrid_oid_to_s(VALUE self);
extern VALUE cubrid_oid_table(VALUE self);
extern VALUE cubrid_oid_refresh(VALUE self);
extern VALUE cubrid_oid_get_value(VALUE self, VALUE attr_name);
extern VALUE cubrid_oid_set_value(VALUE self, VALUE attr_name, VALUE val);
extern VALUE cubrid_oid_save(VALUE self);
extern VALUE cubrid_oid_drop(VALUE self);
extern VALUE cubrid_oid_lock(VALUE self);
extern VALUE cubrid_oid_glo_load(VALUE self, VALUE file_name);
extern VALUE cubrid_oid_glo_save(VALUE self, VALUE file_name);
extern VALUE cubrid_oid_glo_drop(VALUE self);
extern VALUE cubrid_oid_glo_size(VALUE self);
extern VALUE cubrid_oid_each(VALUE self);
extern VALUE cubrid_oid_method_missing(int argc, VALUE* argv, VALUE self);
extern VALUE cubrid_oid_to_hash(VALUE self);

/* CUBRID[http://www.cubrid.com] ruby driver
 * 
 * CUBRID ruby driver는 ruby에서 CUBRID 데이터베이스 서버에 접속하여 질의를 할 수 있도록 해주는 모듈입니다.
 *
 * * Connection
 * * Statement
 * * Oid
 *
 *  con = Cubrid.connect('demodb', '192.168.1.1', '33000', 'foo','bar')
 *  stmt = con.prepare('SELECT * FROM db_user')
 *  stmt.execute
 *  stmt.each { |row|
 *    print row
 *  }
 *  stmt.close
 *  con.close
 *  
 */
void Init_cubrid() 
{
  cCubrid = rb_define_module("Cubrid");
  rb_define_module_function(cCubrid, "connect", cubrid_connect, -1);

  rb_define_const(cCubrid, "CHAR",      INT2NUM(CCI_U_TYPE_CHAR));
  rb_define_const(cCubrid, "VARCHAR",   INT2NUM(CCI_U_TYPE_STRING));
  rb_define_const(cCubrid, "STRING",    INT2NUM(CCI_U_TYPE_STRING));
  rb_define_const(cCubrid, "NCHAR",     INT2NUM(CCI_U_TYPE_NCHAR));
  rb_define_const(cCubrid, "VARNCHAR",  INT2NUM(CCI_U_TYPE_VARNCHAR));
  rb_define_const(cCubrid, "BIT",       INT2NUM(CCI_U_TYPE_BIT));
  rb_define_const(cCubrid, "VARBIT",    INT2NUM(CCI_U_TYPE_VARBIT));
  rb_define_const(cCubrid, "NUMERIC",   INT2NUM(CCI_U_TYPE_NUMERIC));
  rb_define_const(cCubrid, "INT",       INT2NUM(CCI_U_TYPE_INT));
  rb_define_const(cCubrid, "SHORT",     INT2NUM(CCI_U_TYPE_SHORT));
  rb_define_const(cCubrid, "MONETARY",  INT2NUM(CCI_U_TYPE_MONETARY));
  rb_define_const(cCubrid, "FLOAT",     INT2NUM(CCI_U_TYPE_FLOAT));
  rb_define_const(cCubrid, "DOUBLE",    INT2NUM(CCI_U_TYPE_DOUBLE));
  rb_define_const(cCubrid, "DATE",      INT2NUM(CCI_U_TYPE_DATE));
  rb_define_const(cCubrid, "TIME",      INT2NUM(CCI_U_TYPE_TIME));
  rb_define_const(cCubrid, "TIMESTAMP", INT2NUM(CCI_U_TYPE_TIMESTAMP));
  rb_define_const(cCubrid, "SET",       INT2NUM(CCI_U_TYPE_SET));
  rb_define_const(cCubrid, "MULTISET",  INT2NUM(CCI_U_TYPE_MULTISET));
  rb_define_const(cCubrid, "SEQUENCE",  INT2NUM(CCI_U_TYPE_SEQUENCE));
  rb_define_const(cCubrid, "OBJECT",    INT2NUM(CCI_U_TYPE_OBJECT));
  
  rb_define_const(cCubrid, "INCLUDE_OID", INT2NUM(CCI_PREPARE_INCLUDE_OID));
  rb_define_const(cCubrid, "READ_LOCK",   INT2NUM(CCI_OID_LOCK_READ));
  rb_define_const(cCubrid, "WRITE_LOCK",  INT2NUM(CCI_OID_LOCK_WRITE));

  /* connection */
  cConnection  = rb_define_class_under(cCubrid, "Connection", rb_cObject);
  rb_define_method(cConnection, "close", cubrid_conn_close, 0); /* in conn.c */
  rb_define_method(cConnection, "commit", cubrid_conn_commit, 0); /* in conn.c */
  rb_define_method(cConnection, "rollback", cubrid_conn_rollback, 0); /* in conn.c */
  rb_define_method(cConnection, "prepare", cubrid_conn_prepare, -1); /* in conn.c */
  rb_define_method(cConnection, "query", cubrid_conn_query, -1); /* in conn.c */
  rb_define_method(cConnection, "auto_commit?", cubrid_conn_get_auto_commit, 0); /* in conn.c */
  rb_define_method(cConnection, "auto_commit=", cubrid_conn_set_auto_commit, 1); /* in conn.c */
  rb_define_method(cConnection, "to_s", cubrid_conn_to_s, 0); /* in conn.c */
  rb_define_method(cConnection, "glo_new", cubrid_conn_glo_new, 2); /* in conn.c */
  rb_define_method(cConnection, "server_version", cubrid_conn_server_version, 0); /* in conn.c */

  /* statement */
  cStatement  = rb_define_class_under(cCubrid, "Statement", rb_cObject);
  rb_define_method(cStatement, "bind", cubrid_stmt_bind, -1); /* in stmt.c */
  rb_define_method(cStatement, "execute", cubrid_stmt_execute, -1); /* in stmt.c */
  rb_define_method(cStatement, "affected_rows", cubrid_stmt_affected_rows, 0); /* in stmt.c */
  rb_define_method(cStatement, "column_info", cubrid_stmt_column_info, 0); /* in stmt.c */
  rb_define_method(cStatement, "fetch", cubrid_stmt_fetch, 0); /* in stmt.c */
  rb_define_method(cStatement, "fetch_hash", cubrid_stmt_fetch_hash, 0); /* in stmt.c */
  rb_define_method(cStatement, "each", cubrid_stmt_each, 0); /* in stmt.c */
  rb_define_method(cStatement, "each_hash", cubrid_stmt_each_hash, 0); /* in stmt.c */
  rb_define_method(cStatement, "close", cubrid_stmt_close, 0); /* in stmt.c */
  rb_define_method(cStatement, "get_oid", cubrid_stmt_get_oid, 0); /* in stmt.c */
  /* stmt.to_s */

  /* oid */
  cOid = rb_define_class_under(cCubrid, "Oid", rb_cObject);
  rb_define_method(cOid, "to_s", cubrid_oid_to_s, 0); /* in oid.c */
  rb_define_method(cOid, "table", cubrid_oid_table, 0); /* in oid.c */
  rb_define_method(cOid, "[]", cubrid_oid_get_value, 1); /* in oid.c */
  rb_define_method(cOid, "[]=", cubrid_oid_set_value, 2); /* in oid.c */
  rb_define_method(cOid, "each", cubrid_oid_each, 0); /* in oid.c */
  rb_define_method(cOid, "refresh", cubrid_oid_refresh, 0); /* in oid.c */
  rb_define_method(cOid, "save", cubrid_oid_save, 0); /* in oid.c */
  rb_define_method(cOid, "drop", cubrid_oid_drop, 0); /* in oid.c */
  rb_define_method(cOid, "lock", cubrid_oid_lock, 1); /* in oid.c */
  rb_define_method(cOid, "to_hash", cubrid_oid_to_hash, 0); /* in oid.c */
  rb_define_method(cOid, "glo_load", cubrid_oid_glo_load, 1); /* in oid.c */
  rb_define_method(cOid, "glo_save", cubrid_oid_glo_save, 1); /* in oid.c */
  rb_define_method(cOid, "glo_drop", cubrid_oid_glo_drop, 0); /* in oid.c */
  rb_define_method(cOid, "glo_size", cubrid_oid_glo_size, 0); /* in oid.c */
  rb_define_method(cOid, "method_missing", cubrid_oid_method_missing, -1); /* in oid.c */
}

/* Document-class: Cubrid::Connection
 * Connection 클래스는 데이터베이스 서버에 대한 연결을 유지하고 트랜잭션을 연산을 수행합니다.
 */

/* Document-class: Cubrid::Statement
 * Statement 클래스는 질의문을 수행하고 그 결과를 반환합니다.
 */

/* Document-class: Cubrid::Oid
 * Oid 클래스는 CUBRID instance에 대하여 직접 연산을 수행할 수 있도록 합니다.
 * CUBRID는 저장되어 있는 instance들의 고유한 식별자를 OID라는 이름으로 제공합니다. 
 * OID를 통해서 직접 해당하는 instance에 접근하여 read/write/update/delete 연산을 할 수 있습니다.
 * 
 */

/* TODO: 
		 bind method & stored procedure
		 save point
		 db parameter : isolation level, max result count, lock time out, ..
		 glo : load to buffer, save from buffer, performance task
		 schema infomation
*/

