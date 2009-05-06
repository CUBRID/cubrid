#!/bin/sh -f

if [ -z $CUBRID ];
then
    echo "The CUBRID Manager client can not be use, because CUBRID environment was not setted."
    exit 1
fi

### remake cmclient link 
if [ -z $CUBRID_MANAGER ];
then
	CUBRID_MANAGER=$CUBRID/cubridmanager
fi

cd $CUBRID_MANAGER
rm -f cmclient
ln -s cmclient.motif cmclient

### cmclient binary check ###
if [ ! -f $CUBRID_MANAGER/cmclient/.cmclient_bin ]
then
  echo "The CUBRID Manager client not exist!"
  exit 1
fi    

### java environment check ###
emsg=$(java -version 2>&1)
if (($? != 0)); then
    echo "The CUBRID Manager client can not be used, because java environment was not installed or could not be used."
    exit 1
fi

LANG_IS=$LANG

if [ -z $LANG ]
then
  LANG="ko_KR"
fi    

act_LANG=$LANG

if [ ${LANG#ko_} != $LANG ] || [ ${LANG#KO_} != $LANG ] || [ ${LANG#korean} != $LANG ]
then 
    CMLANG="ko_KR"
else
    if [ ${LANG#en_} !=  $LANG ] || [ ${LANG#EN_} !=  $LANG ] 
    then 
        CMLANG="en_US"
    else
        CMLANG="ko_KR"
    fi
fi

export LANG=$CMLANG

cd $CUBRID_MANAGER/cmclient
./.cmclient_bin

export LANG=$act_LANG
