#!/bin/sh

SERVER=$1

echo "drop & create index start"
echo "processing nbd1@"${SERVER}
csql -u dba -S nbd1@${SERVER} -c "drop index i_nbd_comment_article_id_posted_time_d; create index on nbd_comment(article_id, posted_time, recommended_counter desc);"
echo "processing nbd2@"${SERVER}
csql -u dba -S nbd2@${SERVER} -c "drop index i_nbd_comment_article_id_posted_time_d; create index on nbd_comment(article_id, posted_time, recommended_counter desc);"
echo "processing nbd3@"${SERVER}
csql -u dba -S nbd3@${SERVER} -c "drop index i_nbd_comment_article_id_posted_time_d; create index on nbd_comment(article_id, posted_time, recommended_counter desc);"
echo "processing nbd4@"${SERVER}
csql -u dba -S nbd4@${SERVER} -c "drop index i_nbd_comment_article_id_posted_time_d; create index on nbd_comment(article_id, posted_time, recommended_counter desc);"
echo "drop & create index done"
