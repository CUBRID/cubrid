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

extern VALUE cOid;
extern VALUE cubrid_stmt_fetch_one_row(int req_handle, int col_count, T_CCI_COL_INFO *col_info, Connection *con);
extern T_CCI_SET cubrid_stmt_make_set(VALUE data, int u_type);

void
cubrid_oid_free(void *p) 
{
  free(p);
}

VALUE
cubrid_oid_new(Connection *con, char *oid_str)
{
  VALUE oid;
  Oid *o;

  oid = Data_Make_Struct(cOid, Oid, 0, cubrid_oid_free, o);

  o->con = con;
  strcpy(o->oid_str, oid_str);

  return oid;
}

/* call-seq:
 *   to_s() -> string
 * 
 * OID string을 반환합니다.
 */
VALUE 
cubrid_oid_to_s(VALUE self)
{
  Oid *oid;

  Data_Get_Struct(self, Oid, oid);
  return rb_str_new2(oid->oid_str);
}

/* call-seq:
 *   table() -> string
 *
 * OID의 table 이름을 반환합니다.
 */
VALUE 
cubrid_oid_table(VALUE self)
{
  Oid *oid;
  char table_name[MAX_STR_LEN];
  T_CCI_ERROR error;

  Data_Get_Struct(self, Oid, oid);
  CHECK_CONNECTION(oid->con, Qnil);
  
  cci_oid_get_class_name(oid->con->handle, oid->oid_str, table_name, MAX_STR_LEN, &error);

  return rb_str_new2(table_name);
}

/* call-seq:
 *  refresh() -> self
 *
 * 데이터베이스 서버로 부터 OID의 데이터를 읽어옵니다.
 * OID의 컬럼 데이터는 데이터베이스 서버와 자동으로 동기화되지 않습니다.
 * Oid 객체가 생성된 후에 시간이 많이 경과하여 데이터베이스가 갱신되었을 가능성이 있다면 
 * 이 메쏘드를 호출하여 데이터베이스 서버로 부터 최신의 데이터를 읽어올 수 있습니다.
 *
 *  con = Cubrid.connect('demodb')
 *  stmt = con.prepare('SELECT * FROM db_user', CUBRID::INCLUDE_OID)
 *  stmt.execute
 *  stmt.fetch
 *  oid = stmt.get_oid
 *  print oid['name']
 *  #after some time
 *  oid.refresh
 *  print oid['name']
 *  stmt.close
 *  con.close
 *
 */
VALUE 
cubrid_oid_refresh(VALUE self)
{
  Oid *oid;
  int req_handle;
  T_CCI_ERROR error;
  T_CCI_COL_INFO    *col_info;
  T_CCI_SQLX_CMD    sql_type;
  int               col_count, i, res;
  VALUE row, col;
  char *attr_name;

  Data_Get_Struct(self, Oid, oid);
  CHECK_CONNECTION(oid->con, self);
  
  req_handle = cci_oid_get(oid->con->handle, oid->oid_str, NULL, &error);
  if (req_handle < 0) {
    cubrid_handle_error(req_handle, &error);
    return self;
  }
  
  col_info = cci_get_result_info(req_handle, &sql_type, &col_count);
  if (!col_info) {
    cubrid_handle_error(CUBRID_ER_CANNOT_GET_COLUMN_INFO, &error);
    return self;
  }

  res = cci_cursor(req_handle, 1, CCI_CURSOR_CURRENT, &error);    
  if (res < 0) {
    cubrid_handle_error(res, &error);
    return self;
  }

  res = cci_fetch(req_handle, &error);
  if (res < 0) {
    cubrid_handle_error(res, &error);
    return self;
  }

  row = cubrid_stmt_fetch_one_row(req_handle, col_count, col_info, oid->con);

  if (NIL_P(row)) { /* TODO */
    return self;
  }

  oid->hash = rb_hash_new();
  oid->col_type = rb_hash_new();
  oid->col_count = col_count;

  for(i = 0; i < col_count; i++) {
    col = RARRAY(row)->ptr[i];
    attr_name = CCI_GET_RESULT_INFO_NAME(col_info, i+1);
    rb_hash_aset(oid->hash, rb_str_new2(attr_name), col);
    rb_hash_aset(oid->col_type, rb_str_new2(attr_name), INT2NUM(CCI_GET_RESULT_INFO_TYPE(col_info, i+1)));
  }

  cci_close_req_handle(req_handle);
  
  return self;
}

/* call-seq:
 *   [](col_name) -> obj
 *
 * OID의 컬럼 데이터를 반환합니다.
 *
 *  con = Cubrid.connect('demodb')
 *  stmt = con.prepare('SELECT * FROM db_user', CUBRID::INCLUDE_OID)
 *  stmt.execute
 *  stmt.fetch
 *  oid = stmt.get_oid
 *  print oid['name']
 *  print oid.name
 *  stmt.close
 *  con.close
 *
 * 컬럼의 데이터에 접근하는 또다른 방법으로 컬럼의 이름을 메쏘드처럼 사용할 수 있습니다.
 *
 *  oid = stmt.get_oid
 *  print oid.name
 */
VALUE 
cubrid_oid_get_value(VALUE self, VALUE attr_name)
{
  Oid *oid;
  VALUE has_key;
  
  Data_Get_Struct(self, Oid, oid);
  
  if (oid->hash == Qfalse) {
    cubrid_oid_refresh(self);
  }

  has_key = rb_funcall(oid->hash, rb_intern("has_key?"), 1, attr_name);
  if (has_key == Qfalse) {
    rb_raise(rb_eArgError, "Invalid column name");
    return Qnil;
  }
  
  return rb_hash_aref(oid->hash, attr_name);
}

/* call-seq:
 *   []=(col_name, obj) -> nil
 * 
 * OID의 컬럼에 데이터를 저장합니다.
 * 저장된 데이터를 데이터베이스 서버에 반영하기 위해서는 save 메쏘드를 호출하여야 합니다.
 *
 *  con = Cubrid.connect('demodb')
 *  stmt = con.prepare('SELECT * FROM db_user', CUBRID::INCLUDE_OID)
 *  stmt.execute
 *  stmt.fetch
 *  oid = stmt.get_oid
 *  oid['name'] = 'foo'
 *  oid.save
 *  stmt.close
 *  con.close
 *
 * 컬럼의 이름을 메쏘드처럼 사용하여 데이터를 저장할 수 도 있습니다.
 *
 *  oid = stmt.get_oid
 *  oid.name = 'foo'
 *  oid.save
 */
VALUE 
cubrid_oid_set_value(VALUE self, VALUE attr_name, VALUE val)
{
  Oid *oid;
  VALUE has_key;
  
  Data_Get_Struct(self, Oid, oid);
  
  if (oid->hash == Qfalse) {
    cubrid_oid_refresh(self);
  }

  has_key = rb_funcall(oid->hash, rb_intern("has_key?"), 1, attr_name);
  if (has_key == Qfalse) {
    rb_raise(rb_eArgError, "Invalid column name");
    return Qnil;
  }
  
  return rb_hash_aset(oid->hash, attr_name, val);
}

/* call-seq:
 *   save() -> self
 *
 * OID의 데이터를 데이터베이스 서버에 반영합니다.
 */
VALUE 
cubrid_oid_save(VALUE self)
{
  Oid *oid;
  int res, i, u_type;
  T_CCI_ERROR error;
  char **attr_names;
  void **vals;
  int *types;
  VALUE val, keys, key, col_type;
  int *int_val;
  double *dbl_val;
  T_CCI_SET set = NULL;
  T_CCI_DATE *date;
  T_CCI_BIT *bit;
  
  Data_Get_Struct(self, Oid, oid);
  CHECK_CONNECTION(oid->con, Qnil);
  
  if (oid->hash == Qfalse) {
    return self;
  }

  attr_names = (char **) ALLOCA_N(char *, oid->col_count + 1);
  if (attr_names == NULL) {
    rb_raise(rb_eNoMemError, "Not enough memory");
    return self;
  }
  
  attr_names[oid->col_count] = NULL;

  vals = (void **) ALLOCA_N(void *, oid->col_count);
  if (vals == NULL) {
    rb_raise(rb_eNoMemError, "Not enough memory");
    return self;
  }
  
  types = (int *) ALLOCA_N(int, oid->col_count);
  if (types == NULL) {
    rb_raise(rb_eNoMemError, "Not enough memory");
    return self;
  }

  keys = rb_funcall(oid->hash, rb_intern("keys"), 0);
  
  for(i = 0; i < oid->col_count; i++) {
    key = rb_ary_entry(keys, i);
    attr_names[i] = StringValueCStr(key);
    val = rb_hash_aref(oid->hash, key);
    
    switch (TYPE(val)) {
      case T_NIL:
        vals[i] = NULL;
        types[i] = CCI_A_TYPE_STR;
        break;

      case T_FIXNUM:
      case T_BIGNUM:
        int_val = (int *) ALLOCA_N(int, 1);
        if (int_val == NULL) {
          rb_raise(rb_eNoMemError, "Not enough memory");
          return self;
        }
        
        *int_val = NUM2INT(val);
        vals[i] = int_val;
        types[i] = CCI_A_TYPE_INT;
        break;

      case T_FLOAT:
        dbl_val = (double *) ALLOCA_N(double, 1);
        if (dbl_val == NULL) {
          rb_raise(rb_eNoMemError, "Not enough memory");
          return self;
        }

        *dbl_val = NUM2DBL(val);
        vals[i] = dbl_val;
        types[i] = CCI_A_TYPE_DOUBLE;
        break;

      case T_STRING:
        col_type = rb_hash_aref(oid->col_type, key);
        u_type = FIX2INT(col_type);
        
        if (u_type == CCI_U_TYPE_BIT || u_type == CCI_U_TYPE_VARBIT) {
          bit = (T_CCI_BIT *) ALLOCA_N(T_CCI_BIT, 1);
          if (bit == NULL) {
            rb_raise(rb_eNoMemError, "Not enough memory");
            return self;
          }
         
          bit->size = RSTRING(val)->len;
          bit->buf = RSTRING(val)->ptr;
          vals[i] = bit;
          types[i] = CCI_A_TYPE_BIT;
        } else {
          vals[i] = RSTRING(val)->ptr;
          types[i] = CCI_A_TYPE_STR;
        }
        break;

      case T_DATA: 
        if (CLASS_OF(val) == rb_cTime) {
          VALUE a;

          a = rb_funcall(val, rb_intern("to_a"), 0);
          
          date = (T_CCI_DATE *) ALLOCA_N(T_CCI_DATE, 1);
          if (date == NULL) {
            rb_raise(rb_eNoMemError, "Not enough memory");
            return self;
          }
          
          date->ss = FIX2INT(RARRAY(a)->ptr[0]);
          date->mm = FIX2INT(RARRAY(a)->ptr[1]);
          date->hh = FIX2INT(RARRAY(a)->ptr[2]);
          date->day = FIX2INT(RARRAY(a)->ptr[3]);
          date->mon = FIX2INT(RARRAY(a)->ptr[4]);
          date->yr = FIX2INT(RARRAY(a)->ptr[5]);

          vals[i] = date;
          types[i] = CCI_A_TYPE_DATE;
        } else if (CLASS_OF(val) == cOid) {
          Oid *oid;

          Data_Get_Struct(val, Oid, oid);
          vals[i] = oid->oid_str;
          types[i] = CCI_A_TYPE_STR;
        }
        break;

      case T_ARRAY:
        set = cubrid_stmt_make_set(val, CCI_U_TYPE_UNKNOWN);
        vals[i] = set;
        types[i] = CCI_A_TYPE_SET;
        break;

      default:
        rb_raise(rb_eArgError, "Wrong data type");
        return self; /* TODO: avoid leak */
    }
  }
  
  res = cci_oid_put2(oid->con->handle, oid->oid_str, attr_names, vals, types, &error);

  for(i = 0; i < oid->col_count; i++) {
    if (types[i] == CCI_A_TYPE_SET) { 
      cci_set_free(vals[i]);
    }
  }
   
	if (res < 0) {
		cubrid_handle_error(res, &error);
		return INT2NUM(0);
	}

  return self;
}

static VALUE 
cubrid_oid_cmd(VALUE self, T_CCI_OID_CMD cmd)
{
  Oid *oid;
  int res;
  T_CCI_ERROR error;

  Data_Get_Struct(self, Oid, oid);
  CHECK_CONNECTION(oid->con, Qnil);
  
	res = cci_oid(oid->con->handle, cmd, oid->oid_str, &error);
	
	if (res < 0) {
		cubrid_handle_error(res, &error);
		return INT2NUM(0);
	}

  return Qnil;
}

/* call-seq:
 *   drop() -> self
 *
 * OID를 데이터베이스 서버에서 삭제합니다.
 */
VALUE 
cubrid_oid_drop(VALUE self)
{
  return cubrid_oid_cmd(self, CCI_OID_DROP);
}

/* call-seq:
 *   lock(lockmode) -> self
 *
 * OID에 Cubrid::READ_LOCK 또는 Cubrid::WRITE_LOCK 잠금을 설정합니다. 
 * 설정된 잠금은 트랜잭션이 종료될 때 헤제됩니다.
 *
 */
VALUE 
cubrid_oid_lock(VALUE self, VALUE lock)
{
  return cubrid_oid_cmd(self, NUM2INT(lock));
}

/*
stopdoc:
static VALUE
cubrid_oid_column_info(VALUE self)
{
  VALUE           	desc;
  int               req_handle;
  T_CCI_ERROR       error;
  T_CCI_COL_INFO    *col_info;
  T_CCI_SQLX_CMD    sql_type;
  int               col_count, i;
  char              *col_name;
  int               datatype, precision, scale, nullable;
  Oid               *oid;

  Data_Get_Struct(self, Oid, oid);
  CHECK_CONNECTION(oid->con, self);
  
  req_handle = cci_oid_get(oid->con->handle, oid->oid_str, NULL, &error);
  if (req_handle < 0) {
    cubrid_handle_error(req_handle, &error);
    return Qnil;
  }
  
  col_info = cci_get_result_info(req_handle, &sql_type, &col_count);
  if (!col_info) {
    cubrid_handle_error(CUBRID_ER_CANNOT_GET_COLUMN_INFO, &error);
    return Qnil;
  }

  desc = rb_ary_new2(col_count);

  for (i = 0; i < col_count; i++) {
    VALUE item;

    item = rb_hash_new();

    col_name  = CCI_GET_RESULT_INFO_NAME(col_info, i+1);
    precision = CCI_GET_RESULT_INFO_PRECISION(col_info, i+1);
    scale     = CCI_GET_RESULT_INFO_SCALE(col_info, i+1);
    nullable  = CCI_GET_RESULT_INFO_IS_NON_NULL(col_info, i+1);
    datatype  = CCI_GET_RESULT_INFO_TYPE(col_info, i+1);
     
    rb_hash_aset(item, rb_str_new2("name"), rb_str_new2(col_name));
    rb_hash_aset(item, rb_str_new2("type_name"), INT2NUM(datatype));
    rb_hash_aset(item, rb_str_new2("precision"), INT2NUM(precision));
    rb_hash_aset(item, rb_str_new2("scale"), INT2NUM(scale));
    rb_hash_aset(item, rb_str_new2("nullable"), INT2NUM(nullable));

    rb_ary_push(desc, item);
  }

  cci_close_req_handle(req_handle);
  
  return desc;
}
startdoc:
*/

/* call-seq:
 *   glo_load(filename) -> File
 * 
 * 데이터베이스에 저장되어 있는 GLO 데이터를 File로 저장합니다.
 *
 *  con = Cubrid.connect('subway')
 *  con.query('create table attachfile under glo (name string)')
 *  con.commit
 *
 *  glo = con.glo_new('attachfile', 'pic.jpg')
 *  newfile = glo.glo_load('pic_copy.jpg')
 */
VALUE
cubrid_oid_glo_load(VALUE self, VALUE file_name)
{
  Oid *oid;
  int res;
  T_CCI_ERROR error;
  VALUE file, fd;
  
  Data_Get_Struct(self, Oid, oid);
  CHECK_CONNECTION(oid->con, Qnil);
  
  file = rb_file_open(StringValueCStr(file_name), "w");
  fd = rb_funcall(file, rb_intern("fileno"), 0);
  
  res = cci_glo_load(oid->con->handle, oid->oid_str, FIX2INT(fd), &error);
  if (res < 0) {
    cubrid_handle_error(res, &error);
    return Qnil;
  }

  return file;
}

/* call-seq:
 *   glo_save(filename) -> self
 *
 * 주어진 파일의 데이터를 데이터베이스 서버의 GLO로 저장합니다.
 *
 *  con = Cubrid.connect('subway')
 *  con.query('create table attachfile under glo (name string)')
 *  con.commit
 *
 *  glo = con.glo_new('attachfile')
 *  glo.glo_save('pic.jpg')
 */
VALUE
cubrid_oid_glo_save(VALUE self, VALUE file_name)
{
  Oid *oid;
  int res;
  T_CCI_ERROR error;
  
  Data_Get_Struct(self, Oid, oid);
  CHECK_CONNECTION(oid->con, self);
  
  if (NIL_P(file_name)) {
    rb_raise(rb_eArgError, "file name is required.");
    return self;
  }

  res = cci_glo_save(oid->con->handle, oid->oid_str, StringValueCStr(file_name), &error);
  if (res < 0) {
		cubrid_handle_error(res, &error);
		return self;
  }
  
  return self;
}

/* call-seq:
 *   glo_size() -> int
 *
 * GLO에 저장된 데이터의 크기를 반환합니다.
 */
VALUE
cubrid_oid_glo_size(VALUE self)
{
  Oid *oid;
  int size;
  T_CCI_ERROR error;
  
  Data_Get_Struct(self, Oid, oid);
  CHECK_CONNECTION(oid->con, INT2NUM(0));
  
  size = cci_glo_data_size(oid->con->handle, oid->oid_str, &error);
  if (size < 0) {
		cubrid_handle_error(size, &error);
		return INT2NUM(0);
  }
  
  return INT2NUM(size);
}

/* call-seq:
 *   glo_drop() -> self
 *
 * 데이터베이스 서버에서 GLO를 삭제합니다.
 *
 *  con = Cubrid.connect('demodb')
 *  stmt = con.prepare("SELECT * FROM attachfile WHERE name = 'pig.jpg'", CUBRID::INCLUDE_OID)
 *  stmt.execute
 *  stmt.fetch
 *  oid = stmt.get_oid
 *  oid.drop
 */
VALUE
cubrid_oid_glo_drop(VALUE self)
{
  Oid *oid;
  int res;
  T_CCI_ERROR error;
  
  Data_Get_Struct(self, Oid, oid);
  CHECK_CONNECTION(oid->con, self);
  
  res = cci_glo_destroy_data(oid->con->handle, oid->oid_str, &error);
  if (res < 0) {
		cubrid_handle_error(res, &error);
		return self;
  }
  
  return self;
}

/* call-seq:
 *   each() { |name, val| block } -> nil
 *
 * OID의 컬럼 이름과 데이터를 넘겨 주어진 block을 실행합니다.
 *
 *  con = Cubrid.connect('demodb')
 *  stmt = con.prepare('SELECT * FROM db_user', CUBRID::INCLUDE_OID)
 *  stmt.execute
 *  stmt.fetch
 *  oid = stmt.get_oid
 *  oid.each { |name, val|
 *    print name
 *    print val
 *  }
 */
VALUE
cubrid_oid_each(VALUE self)
{
  Oid *oid;

  Data_Get_Struct(self, Oid, oid);
  
  if (oid->hash == Qfalse) {
    cubrid_oid_refresh(self);
  }

  rb_iterate(rb_each, oid->hash, rb_yield, Qnil);
  
  return Qnil;
}

/* call-seq:
 *   to_hash() -> Hash
 *
 * OID의 데이터를 hash로 반환합니다. key는 컬럼의 이름이며, value는 컬럼의 데이터입니다.
 */
VALUE
cubrid_oid_to_hash(VALUE self)
{
  Oid *oid;
  
  Data_Get_Struct(self, Oid, oid);
  
  if (oid->hash == Qfalse) {
    cubrid_oid_refresh(self);
  }

  return oid->hash;
}

VALUE
cubrid_oid_method_missing(int argc, VALUE* argv, VALUE self)
{
  Oid *oid;
  VALUE method, attr_name;
  char *attr_name_str;
  int size, is_set_method = 0;

  Data_Get_Struct(self, Oid, oid);
  
  method = rb_funcall(argv[0], rb_intern("id2name"), 0);
  attr_name_str = StringValueCStr(method);
  size = strlen(attr_name_str);
  
  if (attr_name_str[size - 1] == '=') {
    is_set_method = 1;
    attr_name_str[size - 1] = '\0';
  }
  
  attr_name = rb_str_new2(attr_name_str);

  if (is_set_method) {
    return cubrid_oid_set_value(self, attr_name, argv[1]);
  }
  return cubrid_oid_get_value(self, attr_name);
}

