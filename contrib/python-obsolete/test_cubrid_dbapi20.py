#!/usr/bin/env python
import dbapi20
import unittest
import cubriddb

class test_Cubriddb(dbapi20.DatabaseAPI20Test):
    driver = cubriddb
    connect_args = ()
    connect_kw_args = {'host': 'localhost',
                        'port': 30000,
                        'db' : 'dbapi20_test',
                        'user' : 'dba',
                        'password' : ''}

    def setUp(self):
        # Call superclass setUp In case this does something in the
        # future
        dbapi20.DatabaseAPI20Test.setUp(self) 

        try:
            con = self._connect()
            con.close()
        except:
            print "cannot connect to database."

    def tearDown(self):
        dbapi20.DatabaseAPI20Test.tearDown(self)

    def test_nextset(self): pass
    def test_setoutputsize(self): pass

if __name__ == '__main__':
    unittest.main()
