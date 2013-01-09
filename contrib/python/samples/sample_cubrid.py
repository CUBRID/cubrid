# sample_cubrid.py

import _cubrid
from _cubrid import *

print 'establish connect...'
print

con = _cubrid.connect('CUBRID:localhost:33000:demodb:::', 'public')

print 'server verison:', con.server_version()
print 'client verison:', con.client_version()
print

cur = con.cursor()

print 'create a table - test_cubrid'
print
cur.prepare('DROP TABLE IF EXISTS test_cubrid')
cur.execute()
cur.prepare('CREATE TABLE test_cubrid (id NUMERIC AUTO_INCREMENT(2009122350, 1), name VARCHAR(50))')
cur.execute()

print 'insert some data...'
print
cur.prepare("insert into test_cubrid (name) values ('Zhang San'), ('Li Si'), ('Wang Wu')")
cur.execute()

print 'rowcount:',cur.rowcount
print

print 'last insert id:', con.insert_id()
print 'schema info:', con.schema_info(CUBRID_SCH_TABLE, 'test_cubrid')
print

cur.prepare('insert into test_cubrid (name) values (?),(?)')
cur.bind_param(1, 'Ma Liu')
cur.bind_param(2, 'Niu Qi')
cur.execute()

print 'select data from test_cubrid'
print
cur.prepare('select * from test_cubrid')
cur.execute()

print 'description:'
for item in cur.description:
    print item
print

row = cur.fetch_row()
while row:
    print row
    row = cur.fetch_row()
print

print 'begging to move cursor...'

print 'data_seek(1)'
cur.data_seek(1)
print 'row_tell():', cur.row_tell()
print 'fetch the first row:', cur.fetch_row()
print 'data_seek(3)'
cur.data_seek(3)
print 'row_tell():', cur.row_tell()
print 'row_seek(-1)'
cur.row_seek(-1)
print 'row_tell():', cur.row_tell()
print 'row_seek(2)'
cur.row_seek(2)
print 'row_tell():', cur.row_tell()
print

print 'result info:'
result_infos = cur.result_info()
for item in result_infos:
    print item

cur.close()
con.close()
