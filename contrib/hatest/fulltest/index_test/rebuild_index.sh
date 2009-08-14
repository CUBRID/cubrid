#!/bin/sh

SERVER=$1

echo "alter index start"
echo "processing nbd1@"${SERVER}
csql -u dba -S nbd1@${SERVER} -c "alter index i_nbd_comment_article_id_posted_time_d on nbd_comment(article_id, posted_time, recommended_counter desc) rebuild;"
echo "processing nbd2@"${SERVER}
csql -u dba -S nbd2@${SERVER} -c "alter index i_nbd_comment_article_id_posted_time_d on nbd_comment(article_id, posted_time, recommended_counter desc) rebuild;"
echo "processing nbd3@"${SERVER}
csql -u dba -S nbd3@${SERVER} -c "alter index i_nbd_comment_article_id_posted_time_d on nbd_comment(article_id, posted_time, recommended_counter desc) rebuild;"
echo "processing nbd4@"${SERVER}
csql -u dba -S nbd4@${SERVER} -c "alter index i_nbd_comment_article_id_posted_time_d on nbd_comment(article_id, posted_time, recommended_counter desc) rebuild;"
echo "alter index done"

echo "index check "
echo "nbd1 check "
csql -u dba -S nbd1@${SERVER} -c "select * from db_index where index_name = 'i_nbd_comment_article_id_posted_time_d';"
echo "nbd2 check "
csql -u dba -S nbd2@${SERVER} -c "select * from db_index where index_name = 'i_nbd_comment_article_id_posted_time_d';"
echo "nbd3 check "
csql -u dba -S nbd3@${SERVER} -c "select * from db_index where index_name = 'i_nbd_comment_article_id_posted_time_d';"
echo "nbd4 check "
csql -u dba -S nbd4@${SERVER} -c "select * from db_index where index_name = 'i_nbd_comment_article_id_posted_time_d';"

