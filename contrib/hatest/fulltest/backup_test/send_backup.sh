export BKUP_HOME=/home1/nbd

mkdir -p $BKUP_HOME/backupdb
mkdir -p $BKUP_HOME/backupdb/nbd1
mkdir -p $BKUP_HOME/backupdb/nbd1/backup
mkdir -p $BKUP_HOME/backupdb/nbd1/log
mkdir -p $BKUP_HOME/backupdb/nbd2
mkdir -p $BKUP_HOME/backupdb/nbd2/backup
mkdir -p $BKUP_HOME/backupdb/nbd2/log

#rm -f $BKUP_HOME/backupdb/nbd1/backup/*
#rm -f $BKUP_HOME/backupdb/nbd2/backup/*

mv $CUBRID_DATABASES/nbd1/backup/nbd1_bk0v000 $BKUP_HOME/backupdb/nbd1/backup/.
cp $CUBRID_DATABASES/nbd1/log/nbd1_bkvinf $BKUP_HOME/backupdb/nbd1/log/.
mv $CUBRID_DATABASES/nbd2/backup/nbd2_bk0v000 $BKUP_HOME/backupdb/nbd2/backup/.
cp $CUBRID_DATABASES/nbd2/log/nbd2_bkvinf $BKUP_HOME/backupdb/nbd2/log/.

cp -r $CUBRID_DATABASES/nbd1_d8g674 $BKUP_HOME/backupdb/.
cp -r $CUBRID_DATABASES/nbd2_d8g674 $BKUP_HOME/backupdb/.

cp $CUBRID_DATABASES/databases.txt $BKUP_HOME/backupdb/.

#cd $BKUP_HOME
#tar czf backupdb.tar.gz ./backupdb
#scp backupdb.tar.gz nbd@cdnt14v3.cub:./

cd -
