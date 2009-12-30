#!/bin/bash

RET_SUCCESS=0
RET_FAIL=-1
INSTALL_TMP_DIR=~/.hatest.tmp

# load environment
. hatest.env

SSH_PUB_KEY="ssh-rsa AAAAB3NzaC1yc2EAAAABIwAAAQEAp+lFXDTVi9HgLXmuxOUo/J0s/joEDpQBwbybdtKjwINeQeNdqvok8HgTYbwf7+4APXofQ7LTLweLWLjqXE3DmlUEFTetcqXNvmOxBT4NY27X9KudDaDXkWZ+ohvLTsQO5ep3gyTWy0UJbIN2NX05cVkOXSzrJ4tt3M36FngSlQM80LxcbV4xyEODVxImH9mSF3xNQtbl82HDPnlW8byGVOc1l/7iV8439HuACdByRFfUmno0gk2VhOad5HOhANy2u5Z9wBe6i5anwcCWnUIK24hH15GeuqcBLEMOTQHbDtaHn4XOW5vB34wo4Dq5vJij3gsZ//19zLzYkTNjndNimw== cubrid1@cdnt12v1.cub"

# enable ssh/scp without passwd
setup_ssh()
{
    echo "set up ssh environment at $1@$2"

    ssh $1@$2 "bash -lc \"mkdir -p ~/.ssh && echo $SSH_PUB_KEY >> ~/.ssh/authorized_keys && sort -u -o ~/.ssh/authorized_keys ~/.ssh/authorized_keys && chmod -R 600 ~/.ssh/authorized_keys\""
}

# change gpg- key for yum 
setup_rpm()
{
	echo "import RPM GPG key to $1@$2"
	ssh -t $1@$2 'sudo rpm --import http://61.74.70.44/centos/RPM-GPG-KEY-centos5'
}

# change shell & locale 
change_sh()
{
	echo "change login environment (shell & locale) at $1@$2"
	ssh -t $1@$2 'sudo chsh -s /bin/bash root; sudo sed -i "s/ko_KR.eucKR/en_US.UTF-8/" /etc/sysconfig/i18n'
}

# check heartbeat installed or not 
check_heartbeat_yum_info()
{
    echo "check heartbeat yum info at $1@$2"

	YUM_HEARTBEAT_INFO=`ssh -t $1@$2 "sudo yum info heartbeat 2>&1 | grep \"Repo       : installed\""`

    if [ "x$YUM_HEARTBEAT_INFO" = "x" ]
    then
		echo "Heartbeat is not installed yet !!!!!"
        return $RET_FAIL
    else
        echo "Heartbeat is installed."    
        return $RET_SUCCESS
    fi
}	

# yum install heartbeat
install_heartbeat()
{
	echo "install heartbeat to $1@$2" 	
	ssh -t $1@$2 "sudo yum -y install heartbeat"  
}

setup_env()
{
	setup_ssh $HA_USER $MASTER_HOST
	setup_ssh $HA_USER $SLAVE_HOST

	setup_rpm $1 $2
	change_sh $1 $2

#	check_heartbeat_yum_info $1 $2 	
#	[ $? -ne $RET_SUCCESS ] && install_heartbeat $1 $2 
}


