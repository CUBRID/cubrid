# sample_CUBRIDdb.py

import CUBRIDdb 

con = CUBRIDdb.connect('CUBRID:localhost:33000:demodb', 'public')

cur = con.cursor()

cur.execute('CREATE TABLE test_cubrid (id NUMERIC AUTO_INCREMENT(2009122350, 1), name VARCHAR(50))')

cur.execute("insert into test_cubrid (name) values ('Zhang San'), ('Li Si'), ('Wang Wu'), ('Ma Liu'), ('Niu Qi')")

cur.execute('select * from test_cubrid')

# fetch result use fetchone()
row = cur.fetchone()
#print row
print(row)

# fetch result use fetchmany()
rows = cur.fetchmany(2)
for row in rows:
    #print row
    print(row)

rows = cur.fetchall()
for row in rows:
    #print row
    print(row)
#print rows

cur.close()
con.close()
