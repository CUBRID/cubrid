#!/bin/bash

################################################################################
# 
prog_name='ha_repl_resume.sh'

input_file=

current_host=$(uname -n)
################################################################################

function print_usage()
{
	echo ""
	echo "Usage: $prog_name [options]"
	echo ""
	echo "    -i [input_file]"
	echo ""
}

function is_invalid_args()
{
	[ -z "$input_file" ] && echo " << ERROR >> input_file is not specified" && return 0

	if [ -r "$input_file" ]; then
		return 1
	else		
		echo " << ERROR >> input_file($input_file) is not readable"
		return 0
	fi
}

function ha_repl_resume()
{
	lineno=0	

	while [ $lineno -lt $(wc -l <$input_file) ]
	do
		let lineno=lineno+1
		line=$(head -n $lineno $input_file | tail -n 1)

		echo "$current_host ]$ $line >/dev/null 2>&1 &"
		echo "resume: $line"
		eval "$line >/dev/null 2>&1 &"
	done

	sleep 1

	echo -ne "\n"
	echo -ne " - check heartbeat list on $current_host(master).\n\n"
	echo "$current_host ]$ cubrid heartbeat list" 
	cubrid heartbeat list
	echo -ne "\n\n"

}

### main ##############################

while getopts "i:" optname
do
        case "$optname" in
                "i") input_file="${OPTARG}";;
                "?") print_usage;;
                ":") print_usage;;
                *) print_usage;;
        esac
done

if is_invalid_args; then
	print_usage 
	exit 1
fi

ha_repl_resume

sleep 1

exit 0

