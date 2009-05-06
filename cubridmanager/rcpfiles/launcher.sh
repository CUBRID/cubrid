#!/bin/sh -f

### cmclient binary check ###
if [ ! -f $CUBRID_MANAGER/../cmclient/.cmclient_bin ]
then
  echo "The CUBRID Manager client not exist!"
  exit 0
fi    

### java environment check ###
java -version > /dev/null 2>&1
if [ $? != 0 ]; then
    echo "The CUBRID Manager client can not be used, because java environment was not installed or could not be used."
    exit 0
fi

if [ -z $LANG ]
then
  LANG="ko"
fi    

act_LANG=$LANG

case $LANG in
    ko* )
        CMLANG="ko"
        ;;
    en* )
        CMLANG="en"
        ;;
    * )
        CMLANG="ko"
        ;;
esac

LANG=$CMLANG
export LANG

cd $CUBRID_MANAGER/../cmclient
./.cmclient_bin

LANG=$act_LANG
export LANG
