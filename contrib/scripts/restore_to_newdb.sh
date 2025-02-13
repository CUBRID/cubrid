#!/bin/bash
#
#
#  Copyright 2016 CUBRID Corporation
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
#

# usage message
function show_usage() {
    echo "restore_to_newdb.sh : rename the database name after restoring the database"
    echo "usage : sh $0 [OPTIONS] backuped-database-name new-database-name"
    echo ""
    echo "OPTIONS:"
    echo "  -F path  directory for new database (default: current directory)"
    echo "  -B path  directory for backup volumes (default: current directory)"
    echo "  -d date  restore to specific date (dd-mm-yyyy:hh:mm:ss or 'backuptime')"
    echo "  -l level backup level (0, 1, 2) default: 0 (full backup)"
    echo "  -p       partial recovery if any log archive is absent"
    echo "  -k file  path of key file (_keys) for tde (default: current directory)"
    echo ""
    echo " EXAMPLES"
    echo "  sh $0 backupdb newdb"
    echo "  sh $0 -B /home/cubrid/backup backupdb newdb"
    exit 1
}

# default values
initialize_variables() {
    newdb_path=$(pwd)
    backupdb_path=$(pwd)
    up_to_date=""
    level=0
    partial_recovery=false
    keys_file_path=""
    positional_args=()
}

DEBUG=false

print_debug_info() {
    if [[ "$DEBUG" == "true" ]]; then
    echo "Debug Information:"
    echo "--------------------------"
    echo "newdb_path: $newdb_path"
    echo "backupdb_path: $backupdb_path"
    echo "up_to_date: $up_to_date"
    echo "level: $level"
    echo "partial_recovery: $partial_recovery"
    echo "keys_file_path: $keys_file_path"
    echo "positional_args: ${positional_args[*]}"
    echo "--------------------------"
    fi
}


function check_files() {
    search_file=$1
    debug_str=$2

    if [ -z "$search_file" ]; then
        echo "error: ($debug_str) files not found."
        exit 1
    else
        echo "confirmed: ($debug_str) files found."
        for file in $search_file; do
            printf "\tfile: %s\n" "$file"
        done
    fi
}

function check_volpath() {
    local volpath="$1"
    local datafile="$2"

    # Verify that the databases.txt file exists
    if [[ ! -f "$datafile" ]]; then
        echo "Error: Database file $datafile not found."
        exit 1
    fi

    # Check the vol-path in databases.txt
    local match=$(awk -v path="$volpath" '$2 == path {print $2}' "$datafile")

    # Error if same vol-path exists
    if [[ -n "$match" ]]; then
        echo "Error: newdb_path ($volpath) is the same as an existing vol-path ($match) in $datafile."
        exit 1
    #else
    #    echo ""
    #    echo "Validation passed: newdb_path and vol-path are different."
    fi
}

function check_dbname_uniqueness() {
    local dbname="$1"
    local datafile="$2"

    # Search dbname in databases.txt
    if grep -qw "$dbname" "$datafile"; then
        echo "Error: Database name '$dbname' already exists in $datafile."
        exit 1
    #else
    #    echo ""
    #    echo "Validation passed: '$dbname' does not exist in $datafile."
    fi
}

# parse and validate command-line options and positional arguments
function parse_and_validate_options() {
    local data_file="$CUBRID_DATABASES/databases.txt"

    # Parse command-line options
    while [[ "$#" -gt 0 ]]; do
        case "$1" in
            -F)
                if [[ -z "$2" || "$2" =~ ^- ]]; then
                    echo "Error: -F option requires a value."
                    show_usage
                elif [[ ! -d "$2" ]]; then
                    echo "Error: The directory specified in -F option does not exist: $2"
                    show_usage
                fi
                newdb_path="$2"; shift 2;;
            -B)
                if [[ -z "$2" || "$2" =~ ^- ]]; then
                    echo "Error: -B option requires a directory path."
                    show_usage
                elif [[ ! -d "$2" ]]; then
                    echo "Error: The directory specified in -B option does not exist: $2"
                    show_usage
                fi
                backupdb_path="$2"; shift 2;;
            -d)
                if [[ -z "$2" || "$2" =~ ^- ]]; then
                    echo "Error: -d option requires a value."
                    show_usage
                elif [[ ! "$2" =~ ^([0-2][0-9]|3[0-1])-(0[1-9]|1[0-2])-[0-9]{4}:[0-2][0-9]:[0-5][0-9]:[0-5][0-9]$ ]]; then
                    echo "Error: -d option value must be in the format dd-mm-yyyy:hh:mm:ss"
                    show_usage
                fi
                up_to_date="$2"; shift 2;;
            -l)
                if [[ -z "$2" || "$2" =~ ^- ]]; then
                    echo "Error: -l option requires a value."
                    show_usage
                elif [[ "$2" =~ ^[0-2]$ ]]; then
                    level="$2"
                    shift 2
                else
                    echo "Error: Invalid value for -l. Only 0, 1, or 2 are allowed."
                    show_usage
                fi
                ;;
            -p) partial_recovery=true; shift;;
            -k)
                if [[ -z "$2" || "$2" =~ ^- ]]; then
                    echo "Error: -k option requires a value."
                    show_usage
                fi
                keys_file_path="$2"; shift 2;;
            -*) echo "Unknown option: $1"; show_usage;;
            *) positional_args+=("$1"); shift;;
        esac
    done

    # Validate required dbname arguments
    if [[ "${#positional_args[@]}" -ne 2 ]]; then
        echo "Error: Two positional arguments are required: <backuped-database-name> and <new-database-name>."
        show_usage
    fi
    asis_dbname="${positional_args[0]}"
    tobe_dbname="${positional_args[1]}"

    # asis_dbname and tobe_dbname must not be the same.
    if [[ "$asis_dbname" == "$tobe_dbname" ]]; then
        echo "Error: 'backuped-database-name' and 'new-database-name' cannot be the same."
        exit 1
    fi

    # If the -F option is exists, the vol-path that exists in databases.txt must not be the same as newdb_path.
    if [[ -n "$newdb_path" ]]; then
        # Check if newdb_path is the same as CUBRID_DATABASES path
        if [[ "$newdb_path" == "$CUBRID_DATABASES" ]]; then
            echo "Error: newdb_path ($newdb_path) cannot be the same as CUBRID_DATABASES path ($CUBRID_DATABASES)."
            exit 1
        fi
        check_volpath "$newdb_path" "$data_file"
    fi

    # tobe_dbname should not exist in the databases.txt file
    check_dbname_uniqueness "$tobe_dbname" "$data_file"
}


# verify files exist
function verify_files() {
    local required_files=()

    # Check if the asis_dbname keyfile exists
    if [[ -n "$keys_file_path" ]]; then
        #키 파일 검증
        if [[ ! -f "$keys_file_path" ]]; then
            echo "Error: Key file not found at specified path: $keys_file_path"
            exit 1
        fi
        # 파일 내용 검증(파일이 비어있는지 확인)
        if [[ ! -s "$keys_file_path" ]]; then
            echo "Error: Key file is empty: $keys_file_path"
            exit 1
        fi
    fi

    # Check if the tobe_dbname backup level file exists
    if [[ "$level" -ge 0 ]]; then
        required_files+=("$backupdb_path/${asis_dbname}_bk0v*")
    fi
    if [[ "$level" -ge 1 ]]; then
        required_files+=("$backupdb_path/${asis_dbname}_bk1v*")
    fi
    if [[ "$level" -ge 2 ]]; then
        required_files+=("$backupdb_path/${asis_dbname}_bk2v*")
    fi

    for pattern in "${required_files[@]}"; do
        local files=$(ls $pattern 2>/dev/null)
        if [[ -z "$files" ]]; then
            echo "Error: Required files not found for pattern ($pattern)."
            exit 1
        else
            echo "Confirmed: Files found for pattern ($pattern)."
            for file in $files; do
                printf "\tfile: %s\n" "$file"
            done
        fi
    done


}

# copy and modify databases.txt
function copy_and_modify_databases_txt() {
    asis_cubrid_databases=$CUBRID_DATABASES

    # Check if databases.txt already exists in $newdb_path
    if [ -e "$newdb_path/databases.txt" ]; then
        echo "Warning: $newdb_path/databases.txt already exists."
        #exit 1 #delete

        # Optionally, back up the existing file
        mv "$newdb_path/databases.txt" "$newdb_path/databases.txt.bak" || {
            echo "Failed to back up existing databases.txt."
            #exit 1 #delete
        }
        echo "Existing databases.txt backed up as databases.txt.bak."
    fi

    # Copy the original databases.txt to $newdb_path
    cp "$asis_cubrid_databases/databases.txt" "$newdb_path" || {
        echo "failed to copy databases.txt."
        exit 1
    }

}

# edit databases.txt content
function modify_databases_txt() {
    local file_path="$newdb_path/databases.txt"
    local temp_file=$(mktemp)
    local lob_base_path="${newdb_path}/lob"
    local asis_dbname=$asis_dbname

    while IFS= read -r line; do
        read -a fields <<< "$line"
        if [[ "${fields[0]}" == "$asis_dbname" ]]; then
            fields[1]=$newdb_path
            fields[3]=$newdb_path
            fields[4]="file:$lob_base_path"

            echo "${fields[@]}" >> "$temp_file"
        else
            echo "$line" >> "$temp_file"
        fi
    done < "$file_path"
    mv "$temp_file" "$file_path"
}

# update cubrid_databases environment variable
function update_cubrid_databases() {
    export CUBRID_DATABASES=$newdb_path || {
        echo "failed to update cubrid_databases."
        exit 1
    }
}

# run restore database command
function run_restore_db() {
    local cmd="cubrid restoredb -B $backupdb_path"

    [[ -n "$up_to_date" ]] && cmd+=" -d $up_to_date"
    cmd+=" -l $level"
    $partial_recovery && cmd+=" -p"
    [[ -n "$keys_file_path" ]] && cmd+=" -k $keys_file_path"
    cmd+=" -u -o db_$(date '+%y%m%d%H%M%S').log $asis_dbname"

    eval "$cmd &"
    local cmd_pid=$!
    while kill -0 "$cmd_pid" 2>/dev/null; do
        for s in '/' '-' '' '|'; do
        echo -ne "\rRestoring... $s"
        sleep 1
        done
    done
    wait $cmd_pid
    if [ $? -ne 0 ]; then
        echo "restore db failed."
        exit 1
    fi
}

# rename database
function rename_database() {
    cubrid renamedb "$asis_dbname" "$tobe_dbname" || {
        echo "failed to rename database."
        exit 1
    }
}

# rollback environment variable
function rollback_cubrid_databases() {
    export CUBRID_DATABASES=$asis_cubrid_databases
}


# Function to check file existence, find and append a specific line in databases.txt
function rollback_databases_txt() {
    local rollback_datafile="$CUBRID_DATABASES/databases.txt"
    local newdb_datafile="$newdb_path/databases.txt"
    local tobe_dbname="$tobe_dbname"
    local temp_file=$(mktemp)

    # Check if newdb_datafile exists
    if [ ! -f "$newdb_datafile" ]; then
        echo "Error: $newdb_datafile not found in $rollback_databfile"
        exit 1
    fi

    # Check if tobe_dbname exists
    check_dbname_uniqueness "$tobe_dbname" "$rollback_datafile"

    # Find line number for tobe_dbname
    local search_line
    search_line=$(grep -n "\b$tobe_dbname\b" "$newdb_datafile" | cut -d: -f1)

    if [ -z "$search_line" ]; then
        echo "Error: The specified line with $tobe_dbname not found in $newdb_datafile"
        exit 1
    fi

    # Extract and append the specified line to rollback_datafile
    local content
    content=$(sed -n "${search_line}p" "$newdb_datafile")

    cat "$rollback_datafile" > "$temp_file"
    echo "$content" >> "$temp_file"
    mv "$temp_file" "$rollback_datafile" || {
        echo "Failed to update $rollback_datafile"
        exit 1
    }
    echo "Updated $rollback_datafile successfully."
}

# main execution
#################################################################################
#

    initialize_variables
    parse_and_validate_options "$@"
    verify_files
    copy_and_modify_databases_txt
    modify_databases_txt
    update_cubrid_databases
    run_restore_db
    rename_database
    rollback_cubrid_databases
    rollback_databases_txt
    echo "database restoration completed successfully."
