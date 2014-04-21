#
# Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met: 
#
# - Redistributions of source code must retain the above copyright notice, 
#   this list of conditions and the following disclaimer. 
#
# - Redistributions in binary form must reproduce the above copyright notice, 
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution. 
#
# - Neither the name of the <ORGANIZATION> nor the names of its contributors 
#   may be used to endorse or promote products derived from this software without 
#   specific prior written permission. 
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
# IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
# OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
# OF SUCH DAMAGE. 
#
#

require 'test/unit'
require 'cubrid'

class TestCubrid < Test::Unit::TestCase
  def setup
    @con = Cubrid.connect("demodb")
    sql = "create table t1(s_val short, \
                           i_val int, \
                           f_val float, \
                           d_val double, \
                           n_val numeric(10,5), \
                           m_val monetary, \
                           c_val char(10), \
                           vc_val varchar(100), \
                           str_val string, \
                           dt_val date, \
                           tm_val time, \
                           ts_val timestamp, \
                           b_val bit(10), \
                           vb_val bit varying(100))"
    stmt = @con.prepare sql
    stmt.execute
    @con.commit
    
    sql = "create table t2(s_set set(short), \
                           i_set set(int), \
                           f_set set(float), \
                           d_set set(double), \
                           n_set set(numeric(10,5)), \
                           m_set set(monetary), \
                           c_set set(char(10)), \
                           vc_set set(varchar(100)), \
                           str_set set(string), \
                           dt_set set(date), \
                           tm_set set(time), \
                           ts_set set(timestamp), \
                           b_set set(bit(10)), \
                           vb_set set(bit varying(100)), \
                           o_val t1, \
                           o_set set(t1))"
    stmt = @con.prepare sql
    stmt.execute
    @con.commit

    @con.query("create table glo_test under glo (filename string)");
    @con.commit
    
  end

  def teardown
    stmt = @con.prepare("drop table t1")
    stmt.execute
    @con.commit

    stmt = @con.prepare("drop table t2")
    stmt.execute
    @con.commit

    @con.query("drop table glo_test");
    @con.commit

    @con.close()
  end

  def test_null
    stmt = @con.prepare("insert into t1(s_val) values (null)")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t1(s_val) values (?)")
    assert_equal(1, stmt.execute(nil))
    @con.commit

    stmt = @con.prepare("select s_val from t1")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(nil, row[0])
    row = stmt.fetch
    assert_equal(nil, row[0])
  end

  def test_short
    stmt = @con.prepare("insert into t1(s_val) values (1)")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t1(s_val) values (?)")
    assert_equal(1, stmt.execute(2))
    @con.commit

    stmt = @con.prepare("select s_val from t1")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(1, row[0])
    row = stmt.fetch
    assert_equal(2, row[0])
  end
  
  def test_int
    stmt = @con.prepare("insert into t1(i_val) values (1)")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t1(i_val) values (?)")
    assert_equal(1, stmt.execute(2))
    @con.commit

    stmt = @con.prepare("select i_val from t1")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(1, row[0])
    row = stmt.fetch
    assert_equal(2, row[0])
  end

  def test_float
    stmt = @con.prepare("insert into t1(f_val) values (3.14)")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t1(f_val) values (?)")
    assert_equal(1, stmt.execute(19.9812))
    @con.commit

    stmt = @con.prepare("select f_val from t1")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(3.14, row[0])
    row = stmt.fetch
    assert_in_delta(19.9812, row[0], 0.1)
  end

  def test_double
    stmt = @con.prepare("insert into t1(d_val) values (3.14)")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t1(d_val) values (?)")
    assert_equal(1, stmt.execute(19.9812))
    @con.commit

    stmt = @con.prepare("select d_val from t1")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(3.14, row[0])
    row = stmt.fetch
    assert_equal(19.9812, row[0])
  end

  def test_numeric
    stmt = @con.prepare("insert into t1(n_val) values (3.14)")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t1(n_val) values (?)")
    assert_equal(1, stmt.execute(19.9812))
    @con.commit

    stmt = @con.prepare("select n_val from t1")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(3.14, row[0])
    row = stmt.fetch
    assert_equal(19.9812, row[0])
  end

  def test_monetary
    stmt = @con.prepare("insert into t1(m_val) values (3.14)")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t1(m_val) values (?)")
    assert_equal(1, stmt.execute(19.9812))
    @con.commit

    stmt = @con.prepare("select m_val from t1")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(3.14, row[0])
    row = stmt.fetch
    assert_equal(19.9812, row[0])
  end
 
  def test_char
    stmt = @con.prepare("insert into t1(c_val) values ('hello')")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t1(c_val) values (?)")
    assert_equal(1, stmt.execute('cubrid'))
    @con.commit

    stmt = @con.prepare("select c_val from t1")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal('hello     ', row[0])
    row = stmt.fetch
    assert_equal('cubrid    ', row[0])
  end

  def test_varchar
    stmt = @con.prepare("insert into t1(vc_val) values ('hello')")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t1(vc_val) values (?)")
    assert_equal(1, stmt.execute(1000))
    @con.commit

    stmt = @con.prepare("select vc_val from t1")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal('hello', row[0])
    row = stmt.fetch
    assert_equal('1000', row[0])
  end
 
  def test_string
    stmt = @con.prepare("insert into t1(str_val) values ('hello')")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t1(str_val) values (?)")
    assert_equal(1, stmt.execute(1000))
    @con.commit

    stmt = @con.prepare("select str_val from t1")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal('hello', row[0])
    row = stmt.fetch
    assert_equal('1000', row[0])
  end

  def test_date
    stmt = @con.prepare("insert into t1(dt_val) values ('2007-6-26')")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t1(dt_val) values (?)")
    assert_equal(1, stmt.execute(Time.local(2005, 12, 25)))
    @con.commit

    stmt = @con.prepare("select dt_val from t1")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(Time.local(2007, 6, 26), row[0])
    row = stmt.fetch
    assert_equal(Time.local(2005, 12, 25), row[0])
  end

  def test_time
    stmt = @con.prepare("insert into t1(tm_val) values ('10:25:43')")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t1(tm_val) values (?)")
    assert_equal(1, stmt.execute(Time.local(2005, 12, 25, 9, 59, 59)))
    @con.commit

    stmt = @con.prepare("select tm_val from t1")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(Time.local(1970, 1, 1, 10, 25, 43), row[0])
    row = stmt.fetch
    assert_equal(Time.local(1970, 1, 1, 9, 59, 59), row[0])
  end

  def test_timestamp
    stmt = @con.prepare("insert into t1(ts_val) values ('2007-6-26 10:25:43')")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t1(ts_val) values (?)")
    assert_equal(1, stmt.execute(Time.local(2005, 12, 25, 23, 59, 59)))
    @con.commit

    stmt = @con.prepare("select ts_val from t1")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(Time.local(2007, 6, 26, 10, 25, 43), row[0])
    row = stmt.fetch
    assert_equal(Time.local(2005, 12, 25, 23, 59, 59), row[0])
  end

  def test_bit
    stmt = @con.prepare("insert into t1(b_val) values (B'1010101010')")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t1(b_val) values (?)")
    stmt.bind(1, 'A', Cubrid::BIT);
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("select b_val from t1")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(['1010101010'], row[0].unpack('B10'))
    row = stmt.fetch
    assert_equal(65, row[0][0])
  end

  def test_varbit
    stmt = @con.prepare("insert into t1(vb_val) values (B'1010101010')")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t1(vb_val) values (?)")
    stmt.bind(1, 'ABCD', Cubrid::VARBIT);
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("select vb_val from t1")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(['1010101010'], row[0].unpack('B10'))
    row = stmt.fetch
    assert_equal('ABCD', row[0])
  end

  def test_oid
    stmt = @con.prepare("insert into t1(i_val, str_val) values (1, 'CUBRID') into :x")
    assert_equal(1, stmt.execute)
    @con.commit
  
    stmt = @con.prepare("insert into t2(o_val) values (x)")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("select o_val from t2")
    assert_equal(1, stmt.execute)
    row = stmt.fetch
    assert_equal('t1', row[0].table)
    assert_equal(1, row[0]['i_val'])
    assert_equal(1, row[0].i_val)
    assert_equal('CUBRID', row[0]['str_val'])

    stmt = @con.prepare("insert into t2(o_val) values (?)")
    assert_equal(1, stmt.execute(row[0]))
    @con.commit

    stmt = @con.prepare("select o_val from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    
    assert_equal('t1', row[0].table)
    assert_equal(1, row[0]['i_val'])
    assert_equal('CUBRID', row[0].str_val)
    assert_raise(ArgumentError) { row[0]['nonexistname'] }
    assert_raise(ArgumentError) { row[0].nonexistname }

    row = stmt.fetch
    assert_equal('t1', row[0].table)
    assert_equal(1, row[0]['i_val'])
    assert_equal('CUBRID', row[0]['str_val'])
    
    row[0]['i_val'] = 2
    row[0].str_val = 'HELLO'
    row[0]['s_val'] = 1
    row[0].f_val = 3.141592
    row[0]['c_val'] = 'cubrid'
    row[0].b_val = 'A'
    row[0]['dt_val'] = Time.local(2005, 12, 25)
    assert_raise(ArgumentError) { row[0].nonexistname = 'A' }
    row[0].save
    @con.commit
    
    stmt = @con.prepare("select o_val from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal('t1', row[0].table)
    assert_equal(2, row[0]['i_val'])
    assert_equal('HELLO', row[0]['str_val'])
    row = stmt.fetch
    assert_equal('t1', row[0].table)
    assert_equal(2, row[0]['i_val'])
    assert_equal('HELLO', row[0]['str_val'])
    assert_equal(1, row[0]['s_val'])
    assert_equal(3.141592, row[0]['f_val'])
    assert_equal('cubrid    ', row[0]['c_val'])
    assert_equal(65, row[0]['b_val'][0])
    assert_equal(Time.local(2005, 12, 25), row[0]['dt_val'])   
    
    row[0].each { |key, val| 
      if key == 'i_val'
        assert_equal(2, val)
      elsif key == 'str_val'
        assert_equal('HELLO', val)
      elsif key == 'c_val'
        assert_equal('cubrid    ', val)
      elsif key == 'b_val'
        assert_equal(65, val[0])
      elsif key == 'dt_val'
        assert_equal(Time.local(2005, 12, 25), val)
      end
    }
    
    stmt = @con.prepare("select i_val from t1")
    assert_equal(1, stmt.execute)

    row[0].drop
    @con.commit
    
    stmt = @con.prepare("select i_val from t1")
    assert_equal(0, stmt.execute)

    stmt = @con.prepare("select o_val from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_raise(StandardError) { row[0].refresh }
   end

  def test_short_set
    stmt = @con.prepare("insert into t2(s_set) values ({1, 2, 3})")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t2(s_set) values (?)")
    assert_equal(1, stmt.execute([4, 5, 6]))
    @con.commit

    stmt = @con.prepare("select s_set from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal([1, 2, 3], row[0])
    row = stmt.fetch
    assert_equal([4, 5, 6], row[0])
  end

  def test_int_set
    stmt = @con.prepare("insert into t2(i_set) values ({1, 2, 3})")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t2(i_set) values (?)")
    assert_equal(1, stmt.execute([4, 5, 6]))
    @con.commit

    stmt = @con.prepare("select i_set from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal([1, 2, 3], row[0])
    row = stmt.fetch
    assert_equal([4, 5, 6], row[0])
  end

  def test_float_set
    stmt = @con.prepare("insert into t2(f_set) values ({1.0, 2.26, 3.141592})")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t2(f_set) values (?)")
    assert_equal(1, stmt.execute([987.236, 0.000273642, 5643456.9876543, 0.000273642]))
    @con.commit

    stmt = @con.prepare("select f_set from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal([1.0, 2.26, 3.141592], row[0])
    row = stmt.fetch
    assert_in_delta(0.000273642, row[0][0], 0.1)
    assert_in_delta(987.236, row[0][1], 0.1)
    assert_in_delta(5643456.9876543, row[0][2], 0.1)
  end

  def test_double_set
    stmt = @con.prepare("insert into t2(d_set) values ({1.0, 2.26, 3.141592})")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t2(d_set) values (?)")
    assert_equal(1, stmt.execute([987.236, 0.000273642, 5643456.9876543, 0.000273642]))
    @con.commit

    stmt = @con.prepare("select d_set from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal([1.0, 2.26, 3.141592], row[0])
    row = stmt.fetch
    assert_equal([0.000273642, 987.236, 5643456.9876543], row[0])
  end

  def test_numeric_set
    stmt = @con.prepare("insert into t2(n_set) values ({1.0, 2.26, 3.1415})")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t2(n_set) values (?)")
    assert_equal(1, stmt.execute([987.236, 0.00027, 5643.98765, 0.00027]))
    @con.commit

    stmt = @con.prepare("select n_set from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal([1.0, 2.26, 3.1415], row[0])
    row = stmt.fetch
    assert_equal([0.00027, 987.236, 5643.98765], row[0])
  end

  def test_monetary_set
    stmt = @con.prepare("insert into t2(m_set) values ({1.0, 2.26, 3.141592})")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t2(m_set) values (?)")
    assert_equal(1, stmt.execute([987.236, 0.000273642, 5643456.9876543, 0.000273642]))
    @con.commit

    stmt = @con.prepare("select m_set from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal([1.0, 2.26, 3.141592], row[0])
    row = stmt.fetch
    assert_equal([0.000273642, 987.236, 5643456.9876543], row[0])
  end

  def test_char_set
    stmt = @con.prepare("insert into t2(c_set) values ({'hello', 'zcubrid'})")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t2(c_set) values (?)")
    assert_equal(1, stmt.execute(['hello', 'cubrid']))
    @con.commit

    stmt = @con.prepare("select c_set from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(['hello     ', 'zcubrid   '], row[0])
    row = stmt.fetch
    assert_equal(['cubrid    ', 'hello     '], row[0])
  end

  def test_varchar_set
    stmt = @con.prepare("insert into t2(vc_set) values ({'hello', 'zcubrid'})")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t2(vc_set) values (?)")
    assert_equal(1, stmt.execute(['hello', 'cubrid']))
    @con.commit

    stmt = @con.prepare("select vc_set from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(['hello', 'zcubrid'], row[0])
    row = stmt.fetch
    assert_equal(['cubrid', 'hello'], row[0])
  end

  def test_string_set
    stmt = @con.prepare("insert into t2(str_set) values ({'hello', 'zcubrid'})")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t2(str_set) values (?)")
    assert_equal(1, stmt.execute(['hello', 'cubrid']))
    @con.commit

    stmt = @con.prepare("select str_set from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(['hello', 'zcubrid'], row[0])
    row = stmt.fetch
    assert_equal(['cubrid', 'hello'], row[0])
  end

  def test_date_set
    stmt = @con.prepare("insert into t2(dt_set) values ({'2007-6-26', '2005-12-25'})")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t2(dt_set) values (?)")
    assert_equal(1, stmt.execute([Time.local(2005, 12, 25), Time.local(2005, 12, 26)]))
    @con.commit

    stmt = @con.prepare("select dt_set from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal([Time.local(2005, 12, 25), Time.local(2007, 6, 26)], row[0])
    row = stmt.fetch
    assert_equal([Time.local(2005, 12, 25), Time.local(2005, 12, 26)], row[0])
  end

  def test_time_set
    stmt = @con.prepare("insert into t2(tm_set) values ({'10:20:30', '19:49:3'})")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t2(tm_set) values (?)")
    assert_equal(1, stmt.execute([Time.local(2005, 12, 25, 10, 20, 30), Time.local(2005, 12, 26, 19, 49, 3)]))
    @con.commit

    stmt = @con.prepare("select tm_set from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal([Time.local(1970, 1, 1, 10, 20, 30), Time.local(1970, 1, 1, 19, 49, 3)], row[0])
    row = stmt.fetch
    assert_equal([Time.local(1970, 1, 1, 10, 20, 30), Time.local(1970, 1, 1, 19, 49, 3)], row[0])
  end

  def test_timestamp_set
    stmt = @con.prepare("insert into t2(ts_set) values ({'2007-6-26 10:20:30', '2005-12-25 19:49:3'})")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t2(ts_set) values (?)")
    assert_equal(1, stmt.execute([Time.local(2005, 12, 25, 10, 20, 30), Time.local(2005, 12, 26, 19, 49, 3)]))
    @con.commit

    stmt = @con.prepare("select ts_set from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal([Time.local(2005, 12, 25, 19, 49, 3), Time.local(2007, 6, 26, 10, 20, 30)], row[0])
    row = stmt.fetch
    assert_equal([Time.local(2005, 12, 25, 10, 20, 30), Time.local(2005, 12, 26, 19, 49, 3)], row[0])
  end


  def test_bit_set
    stmt = @con.prepare("insert into t2(b_set) values ({B'1010101010', X'ff'})")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t2(b_set) values (?)")
    stmt.bind(1, ['A', 'B'], Cubrid::SET, Cubrid::BIT);
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("select b_set from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(['1010101010'], row[0][0].unpack('B10'))
    assert_equal(['1111111100'], row[0][1].unpack('B10'))
    row = stmt.fetch
    assert_equal(65, row[0][0][0])
    assert_equal(66, row[0][1][0])
  end

  def test_varbit_set
    stmt = @con.prepare("insert into t2(vb_set) values ({B'1010101010', X'ff'})")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t2(vb_set) values (?)")
    stmt.bind(1, ['ABCD', 'EFGH'], Cubrid::SET, Cubrid::VARBIT);
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("select vb_set from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    assert_equal(['1010101010'], row[0][0].unpack('B10'))
    assert_equal(['11111111'], row[0][1].unpack('B10'))
    row = stmt.fetch
    assert_equal('ABCD', row[0][0])
    assert_equal('EFGH', row[0][1])
  end

  def test_oid_set
    stmt = @con.prepare("insert into t1(i_val) values (1) into :x")
    assert_equal(1, stmt.execute)
    @con.commit
  
    stmt = @con.prepare("insert into t1(i_val) values (2) into :y")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("insert into t2(o_set) values ({x, y})")
    assert_equal(1, stmt.execute)
    @con.commit

    stmt = @con.prepare("select o_set from t2")
    assert_equal(1, stmt.execute)
    row = stmt.fetch
    assert_equal('t1', row[0][0].table)
    assert_equal(1, row[0][0]['i_val'])
    assert_equal('t1', row[0][1].table)
    assert_equal(2, row[0][1]['i_val'])

    stmt = @con.prepare("insert into t2(o_set) values (?)")
    assert_equal(1, stmt.execute(row[0]))
    @con.commit

    stmt = @con.prepare("select o_set from t2")
    assert_equal(2, stmt.execute)
    row = stmt.fetch
    row = stmt.fetch
    assert_equal('t1', row[0][0].table)
    assert_equal(1, row[0][0]['i_val'])
    assert_equal('t1', row[0][1].table)
    assert_equal(2, row[0][1]['i_val'])
  end
  
  def test_transaction
    stmt = @con.prepare("insert into t2(vb_set) values ({B'1010101010', X'ff'})")
    assert_equal(1, stmt.execute)
    @con.commit
    
    stmt = @con.prepare("select * from t2")
    assert_equal(1, stmt.execute)

    stmt = @con.prepare("insert into t2(vb_set) values ({B'1010101010', X'ff'})")
    assert_equal(1, stmt.execute)
    @con.rollback
    
    stmt = @con.prepare("select * from t2")
    assert_equal(1, stmt.execute)
  end

  def test_autocommit
    @con.auto_commit = TRUE;
    assert_equal(TRUE, @con.auto_commit?)

    stmt = @con.prepare("insert into t2(vb_set) values ({B'1010101010', X'ff'})")
    assert_equal(1, stmt.execute)
    @con.rollback
    
    stmt = @con.prepare("select * from t2")
    assert_equal(1, stmt.execute)

    @con.auto_commit = FALSE;
    assert_equal(FALSE, @con.auto_commit?)
    
    stmt = @con.prepare("insert into t2(vb_set) values ({B'1010101010', X'ff'})")
    assert_equal(1, stmt.execute)
    @con.rollback
    
    stmt = @con.prepare("select * from t2")
    assert_equal(1, stmt.execute)
  end
  
  def test_prepare_block
    @con.query("insert into t1(i_val, vc_val, ts_val) values (1, 'CUBRID', '2007-6-27 11:20:0')")
    @con.query("insert into t1(i_val, vc_val, ts_val) values (2, 'Hello', '2007-6-27 11:20:0')")
    @con.commit
    
    @con.prepare("select i_val, vc_val, ts_val from t1 where i_val = ?") { |stmt|
      assert_equal(1, stmt.execute(1))
      stmt.each { |row|
        assert_equal(1, row[0])
        assert_equal('CUBRID', row[1])
        assert_equal(Time.local(2007, 6, 27, 11, 20, 0), row[2])
      }
    }
  end

  def test_query_block
    @con.query("insert into t1(i_val, vc_val, ts_val) values (1, 'CUBRID', '2007-6-27 11:20:0')")
    @con.query("insert into t1(i_val, vc_val, ts_val) values (1, 'CUBRID', '2007-6-27 11:20:0')")
    @con.commit
    
    @con.query("select i_val, vc_val, ts_val from t1") { |row|
      assert_equal(1, row[0])
      assert_equal('CUBRID', row[1])
      assert_equal(Time.local(2007, 6, 27, 11, 20, 0), row[2])
    }
  end

  def test_each_block
    @con.query("insert into t1(i_val, vc_val, ts_val) values (1, 'CUBRID', '2007-6-27 11:20:0')")
    @con.query("insert into t1(i_val, vc_val, ts_val) values (1, 'CUBRID', '2007-6-27 11:20:0')")
    @con.commit
    
    stmt = @con.query("select i_val, vc_val, ts_val from t1")
    assert_equal(2, stmt.affected_rows)

    stmt.each { |row|
      assert_equal(1, row[0])
      assert_equal('CUBRID', row[1])
      assert_equal(Time.local(2007, 6, 27, 11, 20, 0), row[2])
    }
  end
  
  def test_each_hash_block
    @con.query("insert into t1(i_val, vc_val, ts_val) values (1, 'CUBRID', '2007-6-27 11:20:0')")
    @con.query("insert into t1(i_val, vc_val, ts_val) values (1, 'CUBRID', '2007-6-27 11:20:0')")
    @con.commit
    
    stmt = @con.query("select i_val, vc_val, ts_val from t1")
    assert_equal(2, stmt.affected_rows)

    stmt.each_hash { |row|
      assert_equal(1, row['i_val'])
      assert_equal('CUBRID', row['vc_val'])
      assert_equal(Time.local(2007, 6, 27, 11, 20, 0), row['ts_val'])
    }
  end
  
  def test_glo   
    oid = @con.glo_new("glo_test", "a.txt")
    assert_equal(15, oid.glo_size)
    assert_equal(nil, oid.filename)
    
    oid.filename = 'a.txt'
    oid.save
    
    @con.prepare("select * from glo_test") { |stmt|
      stmt.execute
      stmt.each_hash { |row| 
        row.each { |key, val| 
         if key == 'filename' then assert_equal('a.txt', val) end
        }
      }
    }
    
    newfile = oid.glo_load("b.txt")
    assert_equal('b.txt', newfile.path)
    newfile.close   
    assert_equal(15, File.size("b.txt"))
    File.delete("b.txt")

    oid = @con.glo_new("glo_test", nil)
    assert_equal(0, oid.glo_size)
    oid.glo_save("a.txt")
    assert_equal(15, oid.glo_size)
    oid.glo_drop
    assert_raise(StandardError) { oid.glo_size }
  end
end

