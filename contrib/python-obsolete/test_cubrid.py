#!/usr/bin/env python
import dbapi20
import unittest
import cubrid

class test_Cubriddb(unittest.TestCase):
    driver = cubrid
    connect_args = ()
    connect_kw_args = {'host': 'localhost',
                        'port': 30000,
                        'db' : 'cubrid_test',
                        'user' : 'dba',
                        'password' : ''}

    def setUp(self):

    def tearDown(self):

    def test_xxx(self): pass

if __name__ == '__main__':
    unittest.main()
