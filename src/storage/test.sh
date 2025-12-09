echo "stored_procedure=no" >> $CUBRID/conf/cubrid.conf

cubrid server stop demodb
cubrid deletedb demodb
cd $CUBRID/demo
./make_cubrid_demo.sh
cd -

valgrind --leak-check=full --track-origins=yes --trace-children=yes --log-file=valgrind.log cubrid server start demodb
cubrid loaddb -s random_xs_20_angular_schema -d random_xs_20_angular_object -C -u dba demodb -v

cubrid server start demodb

csql -u dba demodb -c "CREATE TABLE random_xs_20_angular_train(id BIGINT, vec VECTOR (20)); CREATE VECTOR INDEX vidx_train ON random_xs_20_angular_train (vec COSINE) WITH (M = 16, ef_construction = 64);"
csql -u dba demodb -i random_xs_20_angular_train.sql

csql -u dba demodb -c " \
set @a = (select vec from random_xs_20_angular_train limit 1); \
select /*+ recompile no_parallel_heap_scan */ id, vec <=> @a from random_xs_20_angular_train order by vec <=> @a limit 5; "
