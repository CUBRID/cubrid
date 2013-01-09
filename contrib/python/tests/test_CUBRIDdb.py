import unittest
import CUBRIDdb
import time
import sys
import decimal
import datetime

class DBAPI20Test(unittest.TestCase):
    driver = CUBRIDdb
    connect_args = ('CUBRID:localhost:33000:demodb:::', 'public', '')
    connect_kw_args = {}
    table_prefix = 'dbapi20test_'

    ddl1 = 'create table %sbooze (name varchar(20))' % table_prefix
    ddl2 = 'create table %sbarflys (name varchar(20))' % table_prefix
    ddl3 = 'create table %sdatatype (col1 int, col2 float, col3 numeric(12,3), \
            col4 time, col5 date, col6 datetime, col7 timestamp)' % table_prefix
    xddl1 = 'drop table if exists %sbooze' % table_prefix
    xddl2 = 'drop table if exists %sbarflys' % table_prefix
    xddl3 = 'drop table if exists %sdatatype' % table_prefix

    def executeDDL1(self, cursor):
        cursor.execute(self.ddl1)

    def executeDDL2(self, cursor):
        cursor.execute(self.ddl2)

    def executeDDL3(self, cursor):
        cursor.execute(self.ddl3)
    
    def setup(self):
        pass

    def tearDown(self):
        pass

    def _check_table_exist(self, connect):
        cursor = connect.cursor()
        cursor.execute(self.xddl1)
        cursor.execute(self.xddl2)
        cursor.execute(self.xddl3)
        connect.commit()
        cursor.close()

    def _connect(self):
        try:
            con = self.driver.connect(
                    *self.connect_args, **self.connect_kw_args
                    )
            self._check_table_exist(con)
            return con
        except AttributeError:
            self.fail("No connect method found in self.driver module")

    def test_connect(self):
        con = self._connect()
        con.close()

    def test_apilevel(self):
        try:
            # Must exist
            apilevel = self.driver.apilevel
            # Must be a valid value
            self.assertEqual(apilevel,'2.0')
        except AttributeError:
            self.fail("Driver doesn't define apilevel")

    def test_paramstyle(self):
        try:
            # Must exist
            paramstyle = self.driver.paramstyle
            # Must be a valid value
            self.failUnless(paramstyle in (
                'qmark', 'numeric', 'format', 'pyformat'
                ))
        except AttributeError:
            self.fail("Driver doesn't define paramstyle")

    # APIS-368
    def test_Exceptions(self):
        # Make sure required exceptions exist, and are in the
        # defined heirarchy.
        self.failUnless(issubclass(self.driver.Error, Exception))
        self.failUnless(
                issubclass(self.driver.InterfaceError, self.driver.Error)
                )
        self.failUnless(
                issubclass(self.driver.DatabaseError,self.driver.Error)
                )

        con = self._connect()
        error = 0
        try:
            cur = con.cursor()
            cur.execute("insert into %sbooze values error_sql ('Hello') " % (self.table_prefix))
        except CUBRIDdb.DatabaseError, e:
            error = 1
        finally:
            con.close()
        #print >>sys.stderr,  "Error %d: %s" % (e.args[0], e.args[1])
        self.assertEqual(error, 1, "catch one except.")

        con = self._connect()
        error = 0
        try:
            cur = con.cursor()
            cur.fetchone()
            cur.execute("insert into %sbooze values ('Hello', 'hello2') " % (self.table_prefix))
        except CUBRIDdb.Error, e:
            error = 1
            #print >>sys.stderr,  "Error %d: %s" % (e.args[0], e.args[1])
        finally:
            con.close()
        self.assertEqual(error, 1, "catch one except. OK.")


    def test_commit(self):
        con = self._connect()
        try:
            # Commit must work, even if it doesn't do anything
            con.commit()
        finally:
            con.close()

    def test_rollback(self):
        con = self._connect()
        try:
            con.rollback()
        finally:
            con.close()

    def test_cursor(self):
        con = self._connect()
        try:
            cur = con.cursor()
        finally:
            cur.close()
            con.close()

    def test_cursor_isolation(self):
        con = self._connect()
        try:
            # Make sure cursors created from the same connection have
            # the documented transaction isolation level
            cur1 = con.cursor()
            cur2 = con.cursor()
            self.executeDDL1(cur1)
            cur1.execute("insert into %sbooze values ('Victoria Bitter')" % (
                self.table_prefix
                ))
            cur2.execute("select name from %sbooze" % self.table_prefix)
            booze = cur2.fetchall()
            self.assertEqual(len(booze), 1)
            self.assertEqual(len(booze[0]), 1)
            self.assertEqual(booze[0][0], 'Victoria Bitter')
        finally:
            con.close()

    def test_description(self):
        con = self._connect()
        try:
            cur = con.cursor()
            self.executeDDL1(cur)
            self.assertEqual(cur.description, None,
                    'cursor.descripton should be none after executing a'
                    'statement that can return no rows (such as DDL)')
            cur.execute('select name from %sbooze' % self.table_prefix)
            self.assertEqual(len(cur.description), 1,
                    'cursor.description describes too many columns')
            self.assertEqual(len(cur.description[0]), 7,
                    'cursor.description[x] tuples must have 7 elements')
            self.assertEqual(cur.description[0][0].lower(), 'name',
                    'cursor.description[x][0] must return column name')
            self.assertEqual(cur.description[0][1], self.driver.STRING,
                    'cursor.description[x][1] must return column type. Got %r'
                        % cur.description[0][1])
            
            # Make sure self.description gets reset
            self.executeDDL2(cur)
            self.assertEqual(cur.description, None,
                    'cursor.description not being set to None when executing '
                    'no-result statments (eg. DDL)')
        finally:
            con.close()
    
    def test_rowcount(self):
        con = self._connect()
        try:
            cur = con.cursor()
            self.executeDDL1(cur)
            self.assertEqual(cur.rowcount, -1,
                    'cursor.rowcount should be -1 after executing '
                    'no-result statements')
            cur.execute("insert into %sbooze value ('Victoria Bitter')" % (
                    self.table_prefix
                    ))
            self.failUnless(cur.rowcount in (-1, 1),
                    'cursor.rowcount should == number or rows inserted, or '
                    'set to -1 after executing an insert statment')
            cur.execute("select name from %sbooze" % self.table_prefix)
            self.failUnless(cur.rowcount in (-1,1),
                    'cursor.rowcount should == number of rows returned, or '
                    'set to -1 after executing a select statement')
            self.executeDDL2(cur)
            self.assertEqual(cur.rowcount, -1,
                    'cursor.rowcount not being reset to -1 after executing '
                    'no-result statements')
        finally:
            con.close()

    def test_close(self):
        con = self._connect()
        try:
            cur = con.cursor()
        finally:
            con.close()

        # cursor.execute should raise an Error if called after connection closed
        #self.assertRaises(self.driver.Error,self.executeDDL1,cur)

        # connection.commit should raise an Error if called after connection
        #self.assertRaises(self.driver.Error,con.commit)

        # connection.close should raise an Error if called more than once
        #self.assertRaises(self.driver.Error,con.close)

    def test_execute(self):
        con = self._connect()
        try:
            cur = con.cursor()
            self._paraminsert(cur)
        finally:
            con.close()

    def _paraminsert(self, cur):
        self.executeDDL1(cur)
        # cur.execute shall return 0 when no row in table
        res = cur.execute('select name from %sbooze' % self.table_prefix)
        self.assertEqual(res, 0,
                'cur.execute should return 0 if a query retrieves no rows')
        cur.execute("insert into %sbooze values ('Victoria Bitter')" % (
            self.table_prefix
            ))
        self.failUnless(cur.rowcount in (-1, 1))

        if self.driver.paramstyle == 'qmark':
            cur.execute(
                'insert into %sbooze values (?)' % self.table_prefix, 
                ("Cooper's",)
                )
        elif self.driver.paramstyle == 'numeric':
            cur.execute(
                'insert into %sbooze values (:1)' % self.table_prefix,
                ("Cooper's",)
                )
        elif self.driver.paramstyle == 'named':
            cur.execute(
                'insert into %sbooze values (:beer)' % self.table_prefix,
                {'beer':"Cooper's"}
                )
        elif self.driver.paramstyle == 'format':
            cur.execute(
                'insert into %sbooze values (%%s)' % self.table_prefix,
                ("Cooper's",)
                )
        elif self.driver.paramstyle == 'pyformat':
            cur.execute(
                'insert into %sbooze values (%%(beer)s' % self.table_prefix,
                {'beer':"Cooper's"}
                )
        else:
            self.fail('Invalid paramstyle')
        self.failUnless(cur.rowcount in (-1, 1))

        cur.execute('select name from %sbooze' % self.table_prefix)
        res = cur.fetchall()
        self.assertEqual(len(res),2,'cursor.fetchall returned too few rows')
        beers = [res[0][0],res[1][0]]
        beers.sort()
        self.assertEqual(beers[0], "Cooper's",
            'cursor.fetchall retrieved incorrect data, or data inserted '
            'incorrectly')
        self.assertEqual(beers[1], "Victoria Bitter",
            'cursor.fetchall retrieved incorrect data, or data inserted '
            'incorrectly')

    def test_executemany(self):
        con = self._connect()
        try:
            cur = con.cursor()
            self.executeDDL1(cur)
            largs = [("Cooper's",), ("Boag's",)]
            margs = [{'beer': "Cooper's"}, {'beer': "Boag's"}]
            if self.driver.paramstyle == 'qmark':
                cur.executemany('insert into %sbooze values (?)' % (
                    self.table_prefix),
                    largs
                    )
            elif self.driver.paramstyle == 'numeric':
                cur.executemany('insert into %sbooze values (:1)' % (
                    self.table_prefix),
                    largs
                    )
            elif self.driver.paramstyle == 'named':
                cur.executemany('insert into %sbooze values (:beer)' % (
                    self.table_prefix),
                    margs
                    )
            elif self.driver.paramstyle == 'format':
                cur.executemany('insert into %sbooze values (%%s)' % (
                    self.table_prefix, largs
                    ))
            elif self.driver.paramstyle == 'pyformat':
                cur.executemany('insert into %sbooze values (%%(beer)s)' % (
                    self.table_prefix),
                    margs
                    )
            else:
                self.fail('Unknown paramstyle')
            cur.execute('select name from %sbooze' % self.table_prefix)
            res = cur.fetchall()
            self.assertEqual(len(res), 2,
                    'cursor.fetchall retrieved incorrect number of rows')
            beers = [res[0][0],res[1][0]]
            beers.sort()
            self.assertEqual(beers[0], "Boag's", 'incorrect data retrieved')
            self.assertEqual(beers[1], "Cooper's", 'incorrect data retrieved')
        finally:
            con.close()

    # APIS-348
    def test_autocommit(self):
        con = self._connect()
        try:
            cur = con.cursor()
            self.executeDDL1(cur)

            con.set_autocommit(False)
            self.assertEqual(con.get_autocommit(), False, "autocommit is off")

            cur.execute("insert into %sbooze values ('Hello')" % (self.table_prefix))
            con.rollback()
            cur.execute("select * from %sbooze" % self.table_prefix)
            rows = cur.fetchall()

            self.assertEqual(len(rows), 0, "0 lines affected")
            
            con.set_autocommit(True)
            self.assertEqual(con.get_autocommit(), True, "autocommit is on")

            cur.execute("insert into %sbooze values ('Hello')" % (self.table_prefix))
            cur.execute("select * from %sbooze" % self.table_prefix)
            rows = cur.fetchall()

            self.assertEqual(len(rows), 1, "1 lines affected")
        finally:
            con.close()

    # APIS-372-373
    def test_datatype(self):
        con = self._connect()
       
        try:
            cur = con.cursor()
            self.executeDDL3(cur)
           
            cur.execute("insert into %sdatatype values (2012, 2012.345, 20.12345,'11:21:30 am',\
                    '2012-10-26', '2012-10-26 11:21:30 am',\
                    '11:21:30 am 2012-10-26')" % self.table_prefix)

            cur.execute("select * from %sdatatype" % self.table_prefix)
            row = cur.fetchone()

            datatypes = [int, float, decimal.Decimal, datetime.time,\
                         datetime.date, datetime.datetime, datetime.datetime]
           
            for i in range(0,7):
                self.assertEqual(isinstance(row[i], datatypes[i]), True,
                        'incorrect data type converted from CUBRID to Python')
        finally:
            con.close()


    def test_fetchone(self):
        con = self._connect()
        try:
            cur = con.cursor()

            # cursor.fetchone should raise an Error if called before
            # executing a select-type query
            #self.assertRaises(self.driver.Error, cur.fetchone)

            # cursor.fetchone should raise an Error if called after
            # executing a query that cannnot return rows
            self.executeDDL1(cur)
            #self.assertRaises(self.driver.Error, cur.fectchone)

            cur.execute('select name from %sbooze' % self.table_prefix)
            self.assertEqual(cur.fetchone(), None,
                    'cursor.fetchone should return None if a query '
                    'retrieves no rows')
            self.failUnless(cur.rowcount in (-1, 0))

            # cursor.fetchone should raise an Error if called after
            # executing a query that cannnot return rows
            cur.execute("insert into %sbooze values ('Victoria Bitter')" % (
                self.table_prefix
                ))
            #self.assertRaises(self.driver.Error, cur.fetchone)

            cur.execute("select name from %sbooze" % self.table_prefix)
            r = cur.fetchone()
            self.assertEqual(len(r), 1,
                    'cursor.fecthone should have retrieved a single row'
                    )
            self.assertEqual(r[0], 'Victoria Bitter',
                    'cursor.fetchone retrieved incorrect data'
                    )
            self.assertEqual(cur.fetchone(), None,
                    'cursor.fetchone should return None if no more rows available')
            self.failUnless(cur.rowcount in (-1, 1))
        finally:
            con.close()

    samples = [
        'Carlton Cold',
        'Carlton Draft',
        'Mountain Goat',
        'Redback',
        'Victoria Bitter',
        'XXXX'
        ]
    
    def _populate(self, cur):
        self.executeDDL1(cur)
        if self.driver.paramstyle == 'qmark':
            cur.executemany('insert into %sbooze values (?)' % (
                self.table_prefix),
                self.samples
                )
        elif self.driver.paramstyle == 'numeric':
            cur.executemany('insert into %sbooze values (:1)' % (
                self.table_prefix),
                self.samples
                )
        elif self.driver.paramstyle == 'named':
            cur.executemany('insert into %sbooze values (:beer)' % (
                self.table_prefix),
                self.samples
                )
        elif self.driver.paramstyle == 'format':
            cur.executemany('insert into %sbooze values (%%s)' % (
                self.table_prefix),
                self.samples
                )
        elif self.driver.paramstyle == 'pyformat':
            cur.executemany('insert into %sbooze values (%%(beer)s)' % (
                self.table_prefix
                ),
                self.samples
                )
        else:
            self.fail('Unknown paramstyle')

    def test_fetchmany(self):
        con = self._connect()
        try:
            cur = con.cursor()

            # cursor.fetchmany should raise an Error if called without
            # issuing a query
            #self.assertRaises(self.driver.Error,cur.fetchmany,4)

            self._populate(cur)
            
            cur.execute('select name from %sbooze' % self.table_prefix)
            r = cur.fetchmany() # should get 1 row
            self.assertEqual(len(r), 1,
                    'cursor.fetchmany retrieved incorrect number of rows, '
                    'default of array is one.')
            cur.arraysize = 10
            r = cur.fetchmany(3) # should get 3 rows
            self.assertEqual(len(r), 3,
                    'cursor.fetchmany retrieved incorrect number of rows')
            r = cur.fetchmany(4) # should get 1 row
            self.assertEqual(len(r), 2,
                    'cursor.fetchmany retrieved incorrect number of rows')
            r = cur.fetchmany(4)
            self.assertEqual(len(r), 0,
                    'cursor.fetchmany should return an empty sequence after '
                    'results are exhausted')
            self.failUnless(cur.rowcount in (-1, 6))

            # Same as above, using cursor.arraysize
            cur.arraysize = 4
            cur.execute('select name from %sbooze' % self.table_prefix)
            r = cur.fetchmany() # should get 4 rows
            self.assertEqual(len(r), 4,
                    'cursor.arraysize not being honoured by fetchmany')
            r = cur.fetchmany() # should get 2 rows more
            self.assertEqual(len(r), 2)
            r = cur.fetchmany() # Should be an empty sequence
            self.assertEqual(len(r),0)
            self.failUnless(cur.rowcount in (-1,6))

            cur.arraysize = 6
            cur.execute('select name from %sbooze' % self.table_prefix)
            rows = cur.fetchmany() # Should get all rows
            self.failUnless(cur.rowcount in (-1,6))
            self.assertEqual(len(rows),6)
            rows = [r[0] for r in rows]
            rows.sort()

            for i in range(0,6):
                self.assertEqual(rows[i], self.samples[i],
                        'incorrect data retrieved by cursor.fetchmany')
            rows = cur.fetchmany()
            self.assertEqual(len(rows), 0,
                    'cursor.fetchmany should return an empty sequence if '
                    'called after the whole result set has been fetched')
            self.failUnless(cur.rowcount in (-1,6))

            self.executeDDL2(cur)
            cur.execute('select name from %sbarflys' % self.table_prefix)
            r = cur.fetchmany()
            self.assertEqual(len(r), 0,
                    'cursor.fetchmany should return an empty sequence '
                    'if query retrieved no rows')
            self.failUnless(cur.rowcount in (-1, 0))
        finally:
            con.close()

    def test_fetchall(self):
        con = self._connect()
        try:
            cur = con.cursor()
            # cursor.fetchall should raise an Error if called without
            # executing a query that may return rows (such as a select)
            #self.assertRaises(self.driver.Error, cur.fetchall)

            self._populate(cur)

            cur.execute('select name from %sbooze' % self.table_prefix)
            rows = cur.fetchall()
            self.failUnless(cur.rowcount in (-1, len(self.samples)))
            self.assertEqual(len(rows), len(self.samples),
                    'cursor.fetchall did not retrieve all rows')
            rows = [r[0] for r in rows]
            rows.sort()
            for i in range(0, len(self.samples)):
                self.assertEqual(rows[i], self.samples[i],
                        'cursor.fetchall retrieved incorrect rows')
            rows = cur.fetchall()
            self.assertEqual(len(rows), 0,
                    'cursor.fetchall should return an empty list if called '
                    'after the whole result set has been fetched')
            self.failUnless(cur.rowcount in (-1, len(self.samples)))
            
            self.executeDDL2(cur)
            cur.execute('select name from %sbarflys' % self.table_prefix)
            rows = cur.fetchall()
            self.failUnless(cur.rowcount in (-1,0))
            self.assertEqual(len(rows), 0,
                    'cursor.fetchall should return an empty list '
                    'if a select query returns no rows')
        finally:
            con.close()

    def test_mixdfetch(self):
        con = self._connect()
        try:
            cur = con.cursor()

            self._populate(cur)

            cur.execute('select name from %sbooze' % self.table_prefix)
            rows1 = cur.fetchone()
            rows23 = cur.fetchmany(2)
            rows4 = cur.fetchone()
            rows56 = cur.fetchall()
            self.failUnless(cur.rowcount in (-1, 6))
            self.assertEqual(len(rows23), 2,
                    'fetchmany returned incorrect number of rows')
            self.assertEqual(len(rows56), 2,
                    'fetchall returned incorrect number of rows')

            rows = [rows1[0]]
            rows.extend([rows23[0][0], rows23[1][0]])
            rows.append(rows4[0])
            rows.extend([rows56[0][0], rows56[1][0]])
            rows.sort()
            for i in range(0, len(self.samples)):
                self.assertEqual(rows[i], self.samples[i],
                        'incorrect data retrieved or inserted')
        finally:
            con.close()

    def test_threadsafety(self):
        try:
            threadsafety = self.driver.threadsafety
            self.failUnless(threadsafety in (0,1,2,3))
        except AttributeError:
            self.fail("Driver doesn't define threadsafety")

    def test_Date(self):
        d1 = self.driver.Date(2011,3,17)
        d2 = self.driver.DateFromTicks(
                time.mktime((2011,3,17,0,0,0,0,0,0)))

    def test_Time(self):
        t1 = self.driver.Time(10, 30, 45)
        t2 = self.driver.TimeFromTicks(
                time.mktime((2011,3,17,17,13,30,0,0,0)))

    def test_Timestamp(self):
        t1 = self.driver.Timestamp(2002,12,25,13,45,30)
        t2 = self.driver.TimestampFromTicks(
                time.mktime((2002,12,25,13,45,30,0,0,0)))

    def test_STRING(self):
        self.failUnless(hasattr(self.driver,'STRING'),
                'module.STRING must be defined')

    def test_BINARY(self):
        self.failUnless(hasattr(self.driver, 'BINARY'),
                'module.BINARY must be defined')
            
    def test_NUMBER(self):
        self.failUnless(hasattr(self.driver,'NUMBER'),
                'module.NUMBER must be defined')

    def test_DATETIME(self):
        self.failUnless(hasattr(self.driver,'DATETIME'),
                'module.DATETIME must be defined')

    def test_ROWID(self):
        self.failUnless(hasattr(self.driver,'ROWID'),
                'module.ROWID must be defined')
        
def suite():
    suite = unittest.TestSuite()
    suite.addTest(DBAPI20Test("test_connect"))
    return suite

if __name__ == '__main__':
    #unittest.main(defaultTest = 'suite')
    #unittest.main()
    suite = unittest.TestLoader().loadTestsFromTestCase(DBAPI20Test)
    unittest.TextTestRunner(verbosity=2).run(suite)

