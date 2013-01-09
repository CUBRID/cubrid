# sample_CUBRIDdb.py

import CUBRIDdb 

con = CUBRIDdb.connect('CUBRID:localhost:33000:demodb:::', 'public')

cur = con.cursor()

cur.execute('DROP TABLE IF EXISTS test_cubrid')

cur.execute('CREATE TABLE test_cubrid (id NUMERIC AUTO_INCREMENT(2009122350, 1), name VARCHAR(50))')

cur.execute("insert into test_cubrid (name) values ('Zhang San'), ('Li Si'), ('Wang Wu'), ('Ma Liu'), ('Niu Qi')")

cur.execute("insert into test_cubrid (name) values (?), (?)", ['Lily', 'John'])

cur.execute("insert into test_cubrid (name) values (?)", ['Tom',])

cur.execute('select * from test_cubrid')

# fetch result use fetchone()
row = cur.fetchone()
print(row)

print('')

# fetch result use fetchmany()
rows = cur.fetchmany(2)
for row in rows:
    print(row)
print("")

rows = cur.fetchall()
for row in rows:
    print(row)

cur.close()
con.close()
