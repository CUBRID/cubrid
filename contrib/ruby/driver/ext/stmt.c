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

extern VALUE cubrid_conn_end_tran(Connection *con, int type);
extern VALUE cubrid_oid_new(Connection *con, char *oid_str);

extern VALUE cStatement, cOid;

void
cubrid_stmt_free(void *p)
{
  free(p);
}

VALUE
cubrid_stmt_new(Connection *con, char *sql, int option)
{
  VALUE cursor;
  Statement *stmt;
  int handle, param_cnt; 
  T_CCI_ERROR error;

  printf("%s\n", sql);

  handle = cci_prepare(con->handle, sql, option, &error);
  if (handle < 0) {
    cubrid_handle_error(handle, &error);
    return Qnil;
  }

  param_cnt = cci_get_bind_num(handle);
  if (param_cnt < 0) {
    cubrid_handle_error(param_cnt, NULL);
    return Qnil;
  } 

  cursor = Data_Make_Struct(cStatement, Statement, 0, cubrid_stmt_free, stmt);
  stmt->con = con;
  stmt->handle = handle;
  stmt->param_cnt = param_cnt;
  stmt->bound = 0;
  
  return cursor;
}

/* call-seq:
 *   close() -> nil
 *
 * Statement를 종료합니다.
 */
VALUE
cubrid_stmt_close(VALUE self)
{
  Statement *stmt;

  GET_STMT_STRUCT(self, stmt);

  if (stmt->handle) {
    cci_close_req_handle(stmt->handle);
    stmt->handle = 0;
  }

  return Qnil;
}

T_CCI_SET
cubrid_stmt_make_set(VALUE data, int u_type) /* TODO: check if all item has same type */
{
  int i, arr_size, res;
  T_CCI_SET set = NULL;
  void *val = NULL;
  int *ind;

  arr_size = RARRAY(data)->len;
  ind = ALLOCA_N(int, arr_size);
  if (ind == NULL) {
    rb_raise(rb_eNoMemError, "Not enough memory");
    return NULL;
  }

  switch (TYPE(rb_ary_entry(data, 0))) {
    case T_FIXNUM:
    case T_BIGNUM:
      {
        int *int_ary = ALLOCA_N(int, arr_size);
        if (int_ary == NULL) {
          rb_raise(rb_eNoMemError, "Not enough memory");
          return NULL;
        }

        for(i = 0; i < arr_size; i++) {
          if (NIL_P(rb_ary_entry(data, i))) {
            ind[i] = 1;
          }
          else {
            int_ary[i] = NUM2INT(rb_ary_entry(data, i));
            ind[i] = 0;
          }
        }

        if (u_type == CCI_U_TYPE_UNKNOWN) {
          u_type = CCI_U_TYPE_INT;
        }
        val = int_ary;
      }
      break;
    case T_FLOAT:
      {
        double *dbl_ary;

        dbl_ary = ALLOCA_N(double, arr_size);
        if (dbl_ary == NULL) {
          rb_raise(rb_eNoMemError, "Not enough memory");
          return NULL;
        }

        for(i = 0; i < arr_size; i++) {
          if (NIL_P(rb_ary_entry(data, i))) {
            ind[i] = 1;
          }
          else {
            dbl_ary[i] = NUM2DBL(rb_ary_entry(data, i));
            ind[i] = 0;
          }
        }

        if (u_type == CCI_U_TYPE_UNKNOWN) {
          u_type = CCI_U_TYPE_DOUBLE;
        }
        val = dbl_ary;
      }
      break;
    case T_STRING:
      {
        if (u_type == CCI_U_TYPE_BIT || u_type == CCI_U_TYPE_VARBIT) {
          T_CCI_BIT *bit_ary;

          bit_ary = ALLOCA_N(T_CCI_BIT, arr_size);
          if (bit_ary == NULL) {
            rb_raise(rb_eNoMemError, "Not enough memory");
            return NULL;
          }

          for(i = 0; i < arr_size; i++) {
            if (NIL_P(rb_ary_entry(data, i))) {
              ind[i] = 1;
            }
            else {
              bit_ary[i].size = RSTRING(rb_ary_entry(data, i))->len;
              bit_ary[i].buf = RSTRING(rb_ary_entry(data, i))->ptr;
              ind[i] = 0;
            }
          }

          val = bit_ary;
         } else {
          char **str_ary;

          str_ary = ALLOCA_N(char*, arr_size);
          if (str_ary == NULL) {
            rb_raise(rb_eNoMemError, "Not enough memory");
            return NULL;
          }

          for(i = 0; i < arr_size; i++) {
            if (NIL_P(rb_ary_entry(data, i))) {
              ind[i] = 1;
            }
            else {
              str_ary[i] = RSTRING(rb_ary_entry(data, i))->ptr;
              ind[i] = 0;
            }
          }

          if (u_type == CCI_U_TYPE_UNKNOWN) {
            u_type = CCI_U_TYPE_STRING;
          }
          val = str_ary;
        }
      }
      break;
    case T_DATA:
      if (CLASS_OF(rb_ary_entry(data, 0)) == rb_cTime) {
        VALUE a;
        T_CCI_DATE *date_ary;

        date_ary = ALLOCA_N(T_CCI_DATE, arr_size);
        if (date_ary == NULL) {
          rb_raise(rb_eNoMemError, "Not enough memory");
          return NULL;
        }

        for(i = 0; i < arr_size; i++) {
          if (NIL_P(rb_ary_entry(data, i))) {
            ind[i] = 1;
          }
          else {
            a = rb_funcall(rb_ary_entry(data, i), rb_intern("to_a"), 0);
            date_ary[i].ss = FIX2INT(RARRAY(a)->ptr[0]);
            date_ary[i].mm = FIX2INT(RARRAY(a)->ptr[1]);
            date_ary[i].hh = FIX2INT(RARRAY(a)->ptr[2]);
            date_ary[i].day = FIX2INT(RARRAY(a)->ptr[3]);
            date_ary[i].mon = FIX2INT(RARRAY(a)->ptr[4]);
            date_ary[i].yr = FIX2INT(RARRAY(a)->ptr[5]);
            
            ind[i] = 0;
          }
        }

        if (u_type == CCI_U_TYPE_UNKNOWN) {
          u_type = CCI_U_TYPE_TIMESTAMP;
        }
        val = date_ary;
      } else if (CLASS_OF(rb_ary_entry(data, 0)) == cOid) {
        char **str_ary;
        Oid *oid;

        str_ary = ALLOCA_N(char*, arr_size);
        if (str_ary == NULL) {
          rb_raise(rb_eNoMemError, "Not enough memory");
          return NULL;
        }

        for(i = 0; i < arr_size; i++) {
          if (NIL_P(rb_ary_entry(data, i))) {
            ind[i] = 1;
          }
          else {
            Data_Get_Struct(rb_ary_entry(data, i), Oid, oid);
            str_ary[i] = oid->oid_str;
            ind[i] = 0;
          }
        }

        if (u_type == CCI_U_TYPE_UNKNOWN) {
          u_type = CCI_U_TYPE_OBJECT;
        }
        val = str_ary;
      }
      break;
    default:
      rb_raise(rb_eArgError, "Wrong data type");
      break;
  }

  res = cci_set_make(&set, u_type, arr_size, val, ind);
  if (res < 0) {
    cubrid_handle_error(res, NULL);
    return NULL;
  }
  
  return set;
}

static void
cubrid_stmt_bind_internal(Statement *stmt, int index, VALUE data, int u_type, int set_type)
{
  int res, int_val, a_type = CCI_A_TYPE_STR;
  char *str_val;
  double dbl_val;
  void *val = NULL;
  T_CCI_SET set = NULL;
  T_CCI_DATE date;
  T_CCI_BIT bit;

  switch (TYPE(data)) {
    case T_NIL:
      a_type = CCI_A_TYPE_STR;
      val = NULL;
      u_type = CCI_U_TYPE_NULL;
      break;

    case T_FIXNUM:
    case T_BIGNUM:
      int_val = NUM2INT(data);
      a_type = CCI_A_TYPE_INT;
      val = &int_val;
      if (u_type == CCI_U_TYPE_UNKNOWN) {
        u_type = CCI_U_TYPE_INT;
      }
      break;

    case T_FLOAT:
      dbl_val = NUM2DBL(data);
      a_type = CCI_A_TYPE_DOUBLE;
      val = &dbl_val;
      if (u_type == CCI_U_TYPE_UNKNOWN) {
        u_type = CCI_U_TYPE_DOUBLE;
      }
      break;

    case T_STRING:
      str_val = RSTRING(data)->ptr;
      a_type = CCI_A_TYPE_STR;
      val = str_val;
      if (u_type == CCI_U_TYPE_UNKNOWN) {
        u_type = CCI_U_TYPE_STRING;
      } else if (u_type == CCI_U_TYPE_BIT || u_type == CCI_U_TYPE_VARBIT) {
        bit.size = RSTRING(data)->len;
        bit.buf = str_val;
        a_type = CCI_A_TYPE_BIT;
        val = &bit;
      }
      break;

    case T_DATA: 
      if (CLASS_OF(data) == rb_cTime) {
        VALUE a;

        a = rb_funcall(data, rb_intern("to_a"), 0);
        date.ss = FIX2INT(RARRAY(a)->ptr[0]);
        date.mm = FIX2INT(RARRAY(a)->ptr[1]);
        date.hh = FIX2INT(RARRAY(a)->ptr[2]);
        date.day = FIX2INT(RARRAY(a)->ptr[3]);
        date.mon = FIX2INT(RARRAY(a)->ptr[4]);
        date.yr = FIX2INT(RARRAY(a)->ptr[5]);

        a_type = CCI_A_TYPE_DATE;
        val = &date;
        if (u_type == CCI_U_TYPE_UNKNOWN) {
          u_type = CCI_U_TYPE_TIMESTAMP;
        }
      } else if (CLASS_OF(data) == cOid) {
        Oid *oid;

        Data_Get_Struct(data, Oid, oid);
        a_type = CCI_A_TYPE_STR;
        val = oid->oid_str;
        if (u_type == CCI_U_TYPE_UNKNOWN) {
          u_type = CCI_U_TYPE_OBJECT;
        }
      }
      break;

    case T_ARRAY:
      set = cubrid_stmt_make_set(data, set_type);
      a_type = CCI_A_TYPE_SET;
      val = set;
      if (u_type == CCI_U_TYPE_UNKNOWN) {
        u_type = CCI_U_TYPE_SET;
      }
      break;

    default:
      rb_raise(rb_eArgError, "Wrong data type");
      return;
  }

  res = cci_bind_param(stmt->handle, index, a_type, val, u_type, 0);

  if (TYPE(data) == T_ARRAY && set) { 
    cci_set_free(set);
  }

  if (res < 0) {
    cubrid_handle_error(res, NULL);
    return;
  }

  return;
}

/* call-seq:
 *   bind(index, data <, db_type, set_type>) -> nil
 *
 * prepare된 Statement의 호스트변수에 데이터를 바인딩합니다.
 * db_type은 ruby의 데이터가 바인딩될 때 적용될 데이터베이스의 타입을 지정합니다.
 * db_type이 주어지지 않으면 아래와 같이 기본 타입으로 적용됩니다.
 *
 *  *fixnum, bignum -> integer
 *  *float          -> double
 *  *string         -> string(varchar)
 *  *Time           -> timestamp
 *  *Oid            -> object
 *  *array          -> collection
 *
 * set_type은 data가 배열인 경우 배열의 원소에 적용될 데이터베이스 타입을 지정합니다.
 * 배열의 모든 원소들은 동일한 타입이어야 합니다.
 * set_type이 주어지지 않으면 배열의 원소들은 기본 타입으로 바인딩됩니다. 
 *   
 *  con = Cubrid.connect('demodb')
 *  con.auto_commit = true
 *  con.query('create table a (a int, b double, c string, d date)')
 *  con.prepare('insert into a values (?, ?, ?, ?)') { |stmt|
 *    stmt.bind(1, 10)
 *    stmt.bind(2, 3.141592)
 *    stmt.bind(3, 'hello')
 *    stmt.bind(4, Time.local(2007, 12, 25, 10, 10, 10), CUBRID::DATE)
 *    stmt.execute
 *  }
 *  con.close
 */
VALUE
cubrid_stmt_bind(int argc, VALUE* argv, VALUE self)
{
  Statement *stmt;
  VALUE index, u_type, data, set_type;

  GET_STMT_STRUCT(self, stmt);
  CHECK_HANDLE(stmt, self);
  
  rb_scan_args(argc, argv, "22", &index, &data, &u_type, &set_type);
  
  if (NIL_P(u_type)) {
    u_type = INT2NUM(CCI_U_TYPE_UNKNOWN);
  }

  if (NIL_P(set_type)) {
    set_type = INT2NUM(CCI_U_TYPE_UNKNOWN);
  }

  cubrid_stmt_bind_internal(stmt, NUM2INT(index), data, NUM2INT(u_type), NUM2INT(set_type));
  stmt->bound = 1;

  return Qnil;
}

static int
cubrid_stmt_is_auto_commitable(T_CCI_SQLX_CMD cmd)
{
  switch(cmd) {
    case SQLX_CMD_SELECT:
    case SQLX_CMD_CALL:
    case SQLX_CMD_CALL_SP:
    case SQLX_CMD_COMMIT_WORK:
    case SQLX_CMD_ROLLBACK_WORK:
    case SQLX_CMD_GET_ISO_LVL:
    case SQLX_CMD_GET_TIMEOUT:
    case SQLX_CMD_GET_OPT_LVL:
    case SQLX_CMD_GET_TRIGGER:
    case SQLX_CMD_SAVEPOINT:
    case SQLX_CMD_GET_LDB:
    case SQLX_CMD_GET_STATS:
      return 0;
  }

  return 1;
}

/* call-seq:
 *   execute()     -> int
 *   execute(...)  -> int
 *
 * prepare된 Statement를 실행하고 검색되거나 영향을 받은 row의 개수를 반환합니다. 
 *
 * 인수가 주어지면 prepare된 Statement의 호스트 변수에 데이터를 바인딩합니다.
 * 인수는 순서대로 호스트 변수에 바인딩 되기 때문에 인수의 개수와 호스트 변수의 개수가 일치해야 합니다.
 * 인수는 묵시적으로 기본 타입으로 바인딩 됩니다. 
 *
 * connection의 auto commit 모드가 true이면 commit이 곧바로 수행됩니다.
 *
 *  con = Cubrid.connect('demodb')
 *  con.prepare('insert into a values (?, ?, ?, ?)') { |stmt|
 *    stmt.execute (10, 3.141592, 'hello', Time.local(2007, 12, 25))
 *  }
 *  con.close
 */
VALUE 
cubrid_stmt_execute(int argc, VALUE* argv, VALUE self)
{
  T_CCI_ERROR       error;
  T_CCI_COL_INFO    *res_col_info;
  T_CCI_SQLX_CMD    res_sql_type;
  int               res_col_count, row_count;
  Statement         *stmt;

  GET_STMT_STRUCT(self, stmt);
  CHECK_HANDLE(stmt, self);

  if (!stmt->bound && stmt->param_cnt != argc) {
    rb_raise(rb_eStandardError, "execute: param_count(%d) != number of argument(%d)", 
             stmt->param_cnt, argc);
    return INT2NUM(0);
  }

  if (argc > 0) {
    int i;
    for (i = 0; i < argc; i++) {
      cubrid_stmt_bind_internal(stmt, i + 1, argv[i], CCI_U_TYPE_UNKNOWN, CCI_U_TYPE_UNKNOWN);
    }
  }

  row_count = cci_execute(stmt->handle, 0, 0, &error);
  if (row_count < 0) {
    cubrid_handle_error(row_count, &error);
    return INT2NUM(0);
  }

  res_col_info = cci_get_result_info(stmt->handle, &res_sql_type, &res_col_count);
  if (res_sql_type == SQLX_CMD_SELECT && !res_col_info) {
    cubrid_handle_error(CUBRID_ER_CANNOT_GET_COLUMN_INFO, &error);
    return INT2NUM(0);
  }

  stmt->col_info = res_col_info;
  stmt->sql_type = res_sql_type;
  stmt->col_count = res_col_count;
  stmt->affected_rows = row_count;

  if(stmt->con->auto_commit == Qtrue && cubrid_stmt_is_auto_commitable(stmt->sql_type)) {
    cubrid_stmt_close(self);
    cubrid_conn_end_tran(stmt->con, CCI_TRAN_COMMIT);
  }

  stmt->bound = 0;
  return INT2NUM(row_count);
}

/* call-seq:
 *   affected_rows() -> int
 *
 * 수행된 SQL로 영향을 받았거나 검색된 row의 개수를 반환합니다.
 */
VALUE 
cubrid_stmt_affected_rows(VALUE self)
{
  Statement *stmt;

  GET_STMT_STRUCT(self, stmt);
  CHECK_HANDLE(stmt, self);

  return INT2NUM(stmt->affected_rows);
}

static VALUE
cubrid_stmt_dbval_to_ruby_value(int req_handle, int type, int index, Connection *con)
{
  int res, ind;
  VALUE val;
  char *res_buf;
  int int_val;
  double double_val;
  T_CCI_DATE date;
  T_CCI_BIT bit;

  switch (type) {
    case CCI_U_TYPE_INT:
    case CCI_U_TYPE_SHORT:
      res = cci_get_data(req_handle, index, CCI_A_TYPE_INT, &int_val, &ind);
      if (res < 0) {
        cubrid_handle_error(res, NULL);
        return Qnil;
      }
      if (ind < 0) {
        val = Qnil;
      } else {
        val = INT2NUM(int_val);
      }
      break;

    case CCI_U_TYPE_FLOAT:
    case CCI_U_TYPE_DOUBLE:
    case CCI_U_TYPE_NUMERIC:
    case CCI_U_TYPE_MONETARY:
      res = cci_get_data(req_handle, index, CCI_A_TYPE_STR, &res_buf, &ind);
      if (res < 0) {
        cubrid_handle_error(res, NULL);
        return Qnil;
      }
      if (ind < 0) {
        val = Qnil;
      } else {
        double_val = atof(res_buf);
        val = rb_float_new(double_val);
      }
      break;

    case CCI_U_TYPE_OBJECT:
      res = cci_get_data(req_handle, index, CCI_A_TYPE_STR, &res_buf, &ind);
      if (res < 0) {
        cubrid_handle_error(res, NULL);
        return Qnil;
      }
      if (ind < 0) {
        val = Qnil;
      } else {
        val = cubrid_oid_new(con, res_buf);
      }
      break;

    case CCI_U_TYPE_DATE:
    case CCI_U_TYPE_TIME:
    case CCI_U_TYPE_TIMESTAMP:
      res = cci_get_data(req_handle, index, CCI_A_TYPE_DATE, &date, &ind);
      if (res < 0) {
        cubrid_handle_error(res, NULL);
        return Qnil;
      }
      if (ind < 0) {
        val = Qnil;
      } else {
        if (type == CCI_U_TYPE_DATE) {
          val = rb_funcall(rb_cTime, rb_intern("mktime"), 3,
              INT2NUM(date.yr), INT2NUM(date.mon), INT2NUM(date.day));
        } else if (type == CCI_U_TYPE_TIME) {
          val = rb_funcall(rb_cTime, rb_intern("mktime"), 7,
              INT2NUM(1970), INT2NUM(1), INT2NUM(1),
              INT2NUM(date.hh), INT2NUM(date.mm), INT2NUM(date.ss), INT2NUM(0));
        } else {
           val = rb_funcall(rb_cTime, rb_intern("mktime"), 7,
              INT2NUM(date.yr), INT2NUM(date.mon), INT2NUM(date.day),
              INT2NUM(date.hh), INT2NUM(date.mm), INT2NUM(date.ss), INT2NUM(0));
        }    
      }
      break;

    case CCI_U_TYPE_BIT:
    case CCI_U_TYPE_VARBIT:
      res = cci_get_data(req_handle, index, CCI_A_TYPE_BIT, &bit, &ind);
      if (res < 0) {
        cubrid_handle_error(res, NULL);
        return Qnil;
      }
      if (ind < 0) {
        val = Qnil;
      } else {
        val = rb_tainted_str_new(bit.buf, bit.size);
      }
      break;

    default:
      res = cci_get_data(req_handle, index, CCI_A_TYPE_STR, &res_buf, &ind);
      if (res < 0) {
        cubrid_handle_error(res, NULL);
        return Qnil;
      }
      if (ind < 0) {
        val = Qnil;
      } else {
        val = rb_str_new2(res_buf);
      }
      break;
  }

  return val;
}

static VALUE
cubrid_stmt_dbval_to_ruby_value_from_set(T_CCI_SET set, int type, int index, Connection *con)
{
  int res, ind;
  VALUE val;
  char *res_buf;
  int int_val;
  double double_val;
  T_CCI_DATE date;
  T_CCI_BIT bit;

  switch (type) {
    case CCI_U_TYPE_INT:
    case CCI_U_TYPE_SHORT:
      res = cci_set_get(set, index, CCI_A_TYPE_INT, &int_val, &ind);
      if (res < 0) {
        cubrid_handle_error(res, NULL);
        return Qnil;
      }
      if (ind < 0) {
        val = Qnil;
      } else {
        val = INT2NUM(int_val);
      }
      break;

    case CCI_U_TYPE_FLOAT:
    case CCI_U_TYPE_DOUBLE:
    case CCI_U_TYPE_NUMERIC:
    case CCI_U_TYPE_MONETARY:
      res = cci_set_get(set, index, CCI_A_TYPE_STR, &res_buf, &ind);
      if (res < 0) {
        cubrid_handle_error(res, NULL);
        return Qnil;
      }
      if (ind < 0) {
        val = Qnil;
      } else {
        double_val = atof(res_buf);
        val = rb_float_new(double_val);
      }
      break;

    case CCI_U_TYPE_OBJECT:
      res = cci_set_get(set, index, CCI_A_TYPE_STR, &res_buf, &ind);
      if (res < 0) {
        cubrid_handle_error(res, NULL);
        return Qnil;
      }
      if (ind < 0) {
        val = Qnil;
      } else {
        val = cubrid_oid_new(con, res_buf);
      }
      break;

    case CCI_U_TYPE_DATE:
    case CCI_U_TYPE_TIME:
    case CCI_U_TYPE_TIMESTAMP:
      res = cci_set_get(set, index, CCI_A_TYPE_DATE, &date, &ind);
      if (res < 0) {
        cubrid_handle_error(res, NULL);
        return Qnil;
      }
      if (ind < 0) {
        val = Qnil;
      } else {
        if (type == CCI_U_TYPE_DATE) {
          val = rb_funcall(rb_cTime, rb_intern("mktime"), 3,
              INT2NUM(date.yr), INT2NUM(date.mon), INT2NUM(date.day));
        } else if (type == CCI_U_TYPE_TIME) {
          val = rb_funcall(rb_cTime, rb_intern("mktime"), 7,
              INT2NUM(1970), INT2NUM(1), INT2NUM(1),
              INT2NUM(date.hh), INT2NUM(date.mm), INT2NUM(date.ss), INT2NUM(0));
        } else {
           val = rb_funcall(rb_cTime, rb_intern("mktime"), 7,
              INT2NUM(date.yr), INT2NUM(date.mon), INT2NUM(date.day),
              INT2NUM(date.hh), INT2NUM(date.mm), INT2NUM(date.ss), INT2NUM(0));
        }    
      }
      break;

    case CCI_U_TYPE_BIT:
    case CCI_U_TYPE_VARBIT:
      res = cci_set_get(set, index, CCI_A_TYPE_BIT, &bit, &ind);
      if (res < 0) {
        cubrid_handle_error(res, NULL);
        return Qnil;
      }
      if (ind < 0) {
        val = Qnil;
      } else {
        val = rb_tainted_str_new(bit.buf, bit.size);
      }
      break;

    default:
      res = cci_set_get(set, index, CCI_A_TYPE_STR, &res_buf, &ind);
      if (res < 0) {
        cubrid_handle_error(res, NULL);
        return Qnil;
      }
      if (ind < 0) {
        val = Qnil;
      } else {
        val = rb_str_new2(res_buf);
      }
      break;
  }

  return val;
}

static VALUE
cubrid_stmt_dbset_to_ruby_value(int req_handle, int index, Connection *con)
{
  int i, res, ind, e_type;
  VALUE val, e;
  T_CCI_SET set = NULL;
  int set_size;

  res = cci_get_data(req_handle, index, CCI_A_TYPE_SET, &set, &ind);
  if (res < 0) {
    cubrid_handle_error(res, NULL);
    return Qnil;
  }

  if (set == NULL)
    return Qnil;

  set_size = cci_set_size(set);
  val = rb_ary_new2(set_size);

  e_type = cci_set_element_type(set);

  for (i = 0; i < set_size; i++) {
    e = cubrid_stmt_dbval_to_ruby_value_from_set(set, e_type, i + 1, con);
    rb_ary_push(val, e);
  } 
  cci_set_free(set); 

  return val;
}

VALUE
cubrid_stmt_fetch_one_row(int req_handle, int col_count, T_CCI_COL_INFO *col_info, Connection *con)
{
  int i, type;
  VALUE row, val;

  row = rb_ary_new();

  for (i = 0; i < col_count; i++) {
    type = CCI_GET_RESULT_INFO_TYPE(col_info, i + 1);

    if (CCI_IS_COLLECTION_TYPE(type)) {
      val = cubrid_stmt_dbset_to_ruby_value(req_handle, i + 1, con);
    } else {
      val = cubrid_stmt_dbval_to_ruby_value(req_handle, type, i + 1, con);
    }

    rb_ary_push(row, val);
  } 

  return row;
}

/* call-seq:
 *   fetch() -> array or nil
 *
 * 검색된 결과에서 현재 커서가 위치한 row를 반환하고 커서의 위치를 다음으로 옮깁니다.
 * row의 column들은 배열에 저장되어 반환됩니다.
 * 더 이상 fetch할 row가 없으면 nil을 반환합니다.
 *
 *  con = Cubrid.connect('demodb')
 *  con.prepare('SELECT * FROM db_user') { |stmt|
 *    stmt.execute
 *    r = stmt.fetch
 *    print r[0]
 *  }
 *  con.close
 *
 * 데이터베이스의 타입은 아래와 같이 루비의 타입으로 대응됩니다.
 *
 *  *int, short                             -> fixnum, bignum
 *  *float, double, numeric, monetary       -> float
 *  *char, varchar, ncahr, varnchar         -> string
 *  *bit, varbit                            -> string
 *  *date, time, timestamp                  -> Time
 *  *object                                 -> Oid
 *  *collection                             -> array
 * 
 */
VALUE 
cubrid_stmt_fetch(VALUE self)
{
  int res;
  T_CCI_ERROR error;
  Statement *stmt;

  GET_STMT_STRUCT(self, stmt);
  CHECK_HANDLE(stmt, self);

  res = cci_cursor(stmt->handle, 1, CCI_CURSOR_CURRENT, &error);
  if (res == CCI_ER_NO_MORE_DATA) {
    return Qnil;
  } else if (res < 0) {
    cubrid_handle_error(res, &error);
    return Qnil;
  }

  res = cci_fetch(stmt->handle, &error);
  if (res < 0) {
    cubrid_handle_error(res, &error);
    return Qnil;
  }

  return cubrid_stmt_fetch_one_row(stmt->handle, stmt->col_count, stmt->col_info, stmt->con);
}

/* call-seq:
 *   fetch_hash() -> hash or nil
 *
 * 검색된 결과에서 현재 커서가 위치한 row를 반환하고 커서의 위치를 다음으로 옮깁니다.
 * row의 column들은 hash에 저장되어 반환됩니다.
 * hash의 key는 column 이름이거나 SELECT 문에 주어진 alias 입니다.
 * 더 이상 fetch할 row가 없으면 nil을 반환합니다.
 * 
 *  con = Cubrid.connect('demodb')
 *  con.prepare('SELECT * FROM db_user') { |stmt|
 *    stmt.execute
 *    r = stmt.fetch_hash
 *    print r['name']
 *  }
 *  con.close
 */
VALUE 
cubrid_stmt_fetch_hash(VALUE self)
{
  VALUE row, col, hash;
  int i;
  char colName[128];
  Statement *stmt;

  GET_STMT_STRUCT(self, stmt);
  CHECK_HANDLE(stmt, self);

  row = cubrid_stmt_fetch(self);
  if (NIL_P(row))
    return Qnil;

  hash = rb_hash_new();
  for(i = 0; i < stmt->col_count; i++) {
    col = RARRAY(row)->ptr[i];
    strcpy(colName, CCI_GET_RESULT_INFO_NAME(stmt->col_info, i+1));
    rb_hash_aset(hash, rb_str_new2(colName), col);
  }

  return hash;
}

/* call-seq:
 *   each() { |row| block } -> nil
 *
 * row를 fetch하여 주어진 block에 전달하여 수행시킵니다. 전달되는 row는 array입니다.
 * block은 row가 fetch될 때마다 수행됩니다. 
 *
 *  con = Cubrid.connect('demodb')
 *  con.prepare('SELECT * FROM db_user') { |stmt|
 *    stmt.execute
 *    stmt.each { |r|
 *      print r[0]
 *    }
 *  }
 *  con.close
 */
VALUE 
cubrid_stmt_each(VALUE self)
{
  VALUE row;

  while(1) {
    row = cubrid_stmt_fetch(self);
	if (NIL_P(row)) {
      break;
	}
    rb_yield(row);
  }
  
  return Qnil;
}

/* call-seq:
 *   each_hash() { |hash| block } -> nil
 *
 * row를 fetch하여 주어진 block에 전달하여 수행시킵니다. 전달되는 row는 hash입니다.
 * block은 row가 fetch될 때마다 수행됩니다. 
 *
 *  con = Cubrid.connect('demodb')
 *  con.prepare('SELECT * FROM db_user') { |stmt|
 *    stmt.execute
 *    stmt.each_hash { |r|
 *      print r['name']
 *    }
 *  }
 *  con.close
 */
VALUE 
cubrid_stmt_each_hash(VALUE self)
{
  VALUE row;

  while(1) {
    row = cubrid_stmt_fetch_hash(self);
	if (NIL_P(row)) {
      break;
	}
    rb_yield(row);
  }
  
  return Qnil;
}

/* call-seq:
 *   column_info() -> array
 *
 * fetch 될 row의 column 정보를 반환합니다. 
 * 한 컬럼의 정보가 hash로 구성되기 때문에, 결국 전체 컬럼 정보는 hash를 저장하고 있는 array로 반환됩니다. 
 * hash의 key는 name, type_name, precision, scale, nullable 이며, 
 * 각각 컬럼 이름, 컬럼의 데이터 타입, 정밀도, 스케일, 널가능 여부를 의미합니다.
 *
 *  con = Cubrid.connect('demodb')
 *  con.prepare('SELECT * FROM db_user') { |stmt|
 *    stmt.column_info.each { |col|
 *      print col['name']
 *      print col['type_name']
 *      print col['precision']
 *      print col['scale']
 *      print col['nullable']
 *    }
 *  }
 *  con.close
 */
VALUE
cubrid_stmt_column_info(VALUE self)
{
  VALUE	desc;
  int i;
  char col_name[MAX_STR_LEN];
  int datatype, precision, scale, nullable;
  Statement *stmt;

  GET_STMT_STRUCT(self, stmt);
  CHECK_HANDLE(stmt, self);

  desc = rb_ary_new2(stmt->col_count);

  for (i = 0; i < stmt->col_count; i++) {
    VALUE item;

    item = rb_hash_new();

    strcpy(col_name, CCI_GET_RESULT_INFO_NAME(stmt->col_info, i+1));
    precision = CCI_GET_RESULT_INFO_PRECISION(stmt->col_info, i+1);
    scale     = CCI_GET_RESULT_INFO_SCALE(stmt->col_info, i+1);
    nullable  = CCI_GET_RESULT_INFO_IS_NON_NULL(stmt->col_info, i+1);
    datatype  = CCI_GET_RESULT_INFO_TYPE(stmt->col_info, i+1);

    rb_hash_aset(item, rb_str_new2("name"), rb_str_new2(col_name));
    rb_hash_aset(item, rb_str_new2("type_name"), INT2NUM(datatype));
    rb_hash_aset(item, rb_str_new2("precision"), INT2NUM(precision));
    rb_hash_aset(item, rb_str_new2("scale"), INT2NUM(scale));
    rb_hash_aset(item, rb_str_new2("nullable"), INT2NUM(nullable));

    rb_ary_push(desc, item);
  }

  return desc;
}

/* call-seq:
 *   get_oid() -> Oid
 *
 * Cubrid::INCLUDE_OID 옵션으로 prepare된 Statement인 경우 실행 결과에 OID가 포함되어 있습니다.
 * get_oid()는 이 OID를 다룰 수 있도록 Oid 객체를 생성하여 반환합니다.
 *
 *  con = Cubrid.connect('demodb')
 *  con.prepare('SELECT * FROM db_user', CUBRID::INCLUDE_OID) { |stmt|
 *    stmt.execute
 *    stmt.fetch
 *    oid = stmt.get_oid
 *    print oid.table
 *  }
 *  con.close
 */
VALUE 
cubrid_stmt_get_oid(VALUE self)
{
  Statement *stmt;
  char oid_str[MAX_STR_LEN];
 
  GET_STMT_STRUCT(self, stmt);
  CHECK_HANDLE(stmt, self);

  cci_get_cur_oid(stmt->handle, oid_str);
  return cubrid_oid_new(stmt->con, oid_str);
}

