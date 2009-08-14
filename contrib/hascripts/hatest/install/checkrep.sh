#!/bin/sh
NOW=`date +%Y/%m/%d-%H:%M:%S`

while [ true ] ;
do
	echo "[ $NOW ] " 

	echo "[ NBD1 ] " 
	csql -u nbd -p nbd -c "select count(*) from nbd_comment" nbd1@d8g674 ; csql -u nbd -p nbd -c "select count(*) from nbd_comment" nbd1@d8g675

	echo "[ NBD2 ] " 
	csql -u nbd -p nbd -c "select count(*) from nbd_comment" nbd2@d8g674 ; csql -u nbd -p nbd -c "select count(*) from nbd_comment" nbd2@d8g675

	echo "[ NBD3 ] " 
	csql -u nbd -p nbd -c "select count(*) from nbd_comment" nbd3@d8g674 ; csql -u nbd -p nbd -c "select count(*) from nbd_comment" nbd3@d8g675

	echo "[ NBD4 ] " 
	csql -u nbd -p nbd -c "select count(*) from nbd_comment" nbd4@d8g674 ; csql -u nbd -p nbd -c "select count(*) from nbd_comment" nbd4@d8g675

	if [ "$1x" = "x" ] 
	then 
		sleep 1
	else 
		sleep $1 
	fi

done
