#sample_lob.py

import _cubrid

con = _cubrid.connect('CUBRID:localhost:33000:demodb', 'public')

cur = con.cursor()

cur.prepare('create table test_lob (image BLOB)')
cur.execute()

lob_bind = con.lob()
lob_bind.imports('123.jpg')

cur.prepare("insert into test_lob values (?)")
cur.bind_lob(1, lob_bind)
cur.execute()

cur.prepare('select * from test_lob')
cur.execute()

lob_fetch = con.lob()
cur.fetch_lob(1, lob_fetch)
lob_fetch.export('123.out')

lob_bind.close()
lob_fetch.close()

cur.close()
con.close()
