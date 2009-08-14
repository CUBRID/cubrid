#!/bin/sh

verbose=0
force=0
resource=""
nodename=""

vecho ()
{
	[ $verbose -eq 1 ] && echo $@
}

usage ()
{
	echo ""
	echo "Tool for change resource's role to MASTER"
	echo "  Usage $0 [OPTIONS] <resource name>"
	echo "   options:"
	echo "           -v	 turn on verbose mode"
	echo "           -f	 turn on force mode (never prompt)"
	echo "           -h	 node name for resource"
	echo "   <resource name> resource name for change role to Master"

	exit 1;
}

checkmaster ()
{
	# Check master with $1 resource and $2 nodename
	checking=`crm_mon -1 | grep -w "$1" | grep -w "$2" | grep -w "Master"`
	[ "x$checking" == "x" ] && return 1 || return 7
}

waitmaster ()
{
  	echo -n "Changing role of resource $1 at $2 to Master"
	for i in `seq 5`
	do
		checkmaster $1 $2
		if [ $? -eq 7 ]
		then
			return 7
		else
			echo -n "."
			sleep 3
		fi
	done

	checkmaster $1 $2
	return $?
}

changemaster ()
{
	# change master with $1 resource and $2 nodename
	# get value
	oldval=`crm_attribute -G -t status -n "master-$1" -Q -U $2`
	if [ $? -eq 0 ]
	then
	  repaircmd="crm_attribute -v $oldval -t status -n "master-$1" -Q -U $2"
	else
	  repaircmd="crm_attribute -D -t status -n "master-$1" -Q -U $2"
	fi

	# set value
	crm_attribute -v INFINITY -t status -n "master-$1" -Q -U $2

	trap "echo Quitting...;eval $repaircmd;exit 234" INT TERM

	# check to sure
	waitmaster $1 $2
	[ $? -eq 7 ] && echo "Changed" || echo "Failed"

	trap - INT TERM

	# repair original value
	eval $repaircmd;
}


# obsolete function for change master role
changemaster_old ()
{
	# change master with $1 resource and $2 nodename (old fashion)
	# get node uuid
	nodeid=`crmadmin -N | grep -w "$nodename" | sed -e "s/normal node: "$nodename" (\(.*\))/\1/g"`
	if [ "x$nodeid" == "x" ]
	then
		echo "Error: Cannot retreive node ID for resource $1"
		exit 1
	fi

	nvpairid="status-master-$1-$2"
	nvparename="master-$1"
	# get value
	command="cibadmin -o status -Q | grep 'id=\"$nvpairid\" name=\"$nvparename\"'"
	original=`eval "$command"`
	new=`echo $original | sed -e 's/value=".*"/value="INFINITY"/g'`

	# set value
	command="cibadmin -M -X '$new'"
	repaircommand="cibadmin -M -X '$original'"

	eval $command

	trap "echo Quitting...;eval $repaircommand;exit 234" INT TERM

	# check to sure
	waitmaster $1 $2
	[ $? -eq 7 ] && echo "Changed" || echo "Failed"

	trap - INT TERM

	# repair orignal value
	eval $repaircommand
}

while getopts ":vfh:" optname
do
	case "$optname" in
        "v")
	  verbose=1
          vecho "Option $optname for verbose mode is turned on"
          ;;
        "f")
	  force=1
          vecho "Option $optname for force mode is turned on"
          ;;
        "h")
          vecho "Option $optname for nodename has value $OPTARG"
	  nodename=$OPTARG
          ;;
        "?")
          echo "Unknown option $OPTARG"
	  usage
          ;;
        ":")
          echo "No argument value for option $OPTARG"
	  usage
          ;;
        *)
        # Should not occur
          echo "Unknown error while processing options"
	  usage
          ;;
	esac
done
shift `expr $OPTIND - 1`

resource=$@
if [ "x$resource" = "x" ]
then
	echo "Error: Resource name is not specified"
	usage
else
	set -- $resource
	vecho "number of resource are $# ($@)"
	if [ $# -ne 1 ]
	then
		echo -n "Error: Multiple resouce name ("
		for res in $@
		do
			echo -n "$res, "
		done
		echo -ne "\b\b"
		echo ") is not allowed"
		usage
	fi
fi

# get node name of given resource
resourcenode=`crm_resource -W -r $resource -Q`
if [ "x$nodename" = "x" ]
then
	[ "x$resourcenode" = "x" ] || nodename=$resourcenode
else
	if [ "$nodename" != "$resourcenode" ]
	then
		echo "Error: There is no resource $resource at node $nodename"
		exit 1
	else
		# same nodename
		force=1
	fi
fi

# recheck node
checkmaster $resource $nodename
if [ $? -eq 7 ]
then
	echo "Error: Role of $resource at $nodename is MASTER already"
	exit 1;
fi

if [ $force -ne 1 ]
then
	echo "CAUTION: Role of $resource at $nodename will be changed to MASTER"
	echo -n "Are you sure to continue? [y|N] : "
	read yn
	if [ "$yn" != "y" -a "$yn" != "Y" ]
	then
		echo "Aborted."
		exit 0
	fi
fi

changemaster $resource $nodename

