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

. ~/.cubrid.sh


# Usage
usage() {
    echo "rename_to_newdb.sh : Convert a backup-file to a new-DB"
    echo "Usage : sh rename_to_newdb.sh [OPTION]  ASIS_DBNAME TOBE_DBNAME"
    echo ""
    echo "Valid options:"
    echo "  -F : The -F option specifies an absolute path to a directory where the new database will be created."
    echo "  -B : This option specifies the absolute path to the directory where the backup exists, and if the -B option is not specified, the backup file is found in the current working directory."
    echo "  -d : restore the database up to its condition at given DATE"
    echo "  -l : LEVEL of backup to be restored; see backup usage"
    echo "  -p : perform partial recovery if any log archive is absent"
    echo "  -k : Path of key file (_keys) for TDE to be used during restoring"
    exit 1
}

# Default values
F_OPTION=$(pwd)
B_OPTION=$(pwd)
UP_TO_DATE=""
LEVEL=0
PARTIAL_RECOVERY=false
KEYS_FILE_PATH=""


# Parse options
while [[ "$#" -gt 0 ]]; do
    case "$1" in
        -F)
            F_OPTION="$2"
            shift 2
            ;;
        -B)
            B_OPTION="$2"
            shift 2
            ;;
        -d)
            UP_TO_DATE="$2"
            shift 2
            ;;
        -l)
            if [[ "$2" =~ ^[0-2]$ ]]; then
                LEVEL="$2"
                shift 2
            else
                echo "Error: Invalid value for -l. Only 0, 1, or 2 are allowed."
                usage
            fi
            ;;
        -p)
            PARTIAL_RECOVERY=true
            shift
            ;;
        -k)
            KYE_FILE_PATH="$2"
            shift 2
            ;;
        -*)
            echo "Unknown option: $1"
            usage
            ;;
        *)
            # Treat as positional argument (ASIS_DBNAME, TOBE_DBNAME)
            POSITIONAL_ARGS+=("$1")
            shift
            ;;
    esac
done


# Check for exactly two positional arguments (ASIS_DBNAME, TOBE_DBNAME)
if [[ "${#POSITIONAL_ARGS[@]}" -ne 2 ]]; then
    usage
fi
ASIS_DBNAME="${POSITIONAL_ARGS[0]}"
TOBE_DBNAME="${POSITIONAL_ARGS[1]}"

TOBE_FULLPATH=$F_OPTION/$TOBE_DBMANE

# Check TOBE_DBNAME Directory
if [ ! -d "$TOBE_FULLPATH" ]; then
    echo ""
    echo "Error: TOBE_FULLPATH("$TOBE_FULLPATH") does not exist."
    exit 1
else
    echo "Confirmed : ("$TOBE_FULLPATH") exist"
fi

# Check result
echo "ASIS_DBNAME: $ASIS_DBNAME"
echo "TOBE_DBNAME: $TOBE_DBNAME"
echo "-F: ${F_OPTION:-none}"
echo "-B: ${B_OPTION:-none}"
echo "-d: ${UP_TO_DATE:-none}"
echo "-l: ${LEVEL:-none}"
echo "-p: ${PARTIAL_RECOVERY:-none}"
echo "-k: ${KYE_FILE_PATH:-none}"


# Verify that the file of type ASIS_DBNAME_bk* exists
#backup_files=$(ls "$F_OPTION"/"$ASIS_DBNAME"_bk* 2> /dev/null)
backup_files=$(ls "$B_OPTION"/"$ASIS_DBNAME"_bk* 2> /dev/null)

if [ -z "$backup_files" ]; then
    echo "Error: No (${ASIS_DBNAME}_bk*) files not found in new DATABASES."
    exit 1
else
    echo "Confirmed : (${ASIS_DBNAME}_bk*) exist"
    #echo -e "\tfiles : %s\n" $backup_files
    for file in $backup_files
    do
        printf "\tfiles : %s\n" "$file"
    done
fi


# ASIS DB의 $CUBRID_DATABASES
ASIS_CUBRID_DATABASES=$CUBRID_DATABASES

# Copy ASIS databases.txt to TOBE databases.txt
echo -n "COPY databases.txt $F_OPTION"
RUN_COPY_TXT=$(cp $ASIS_CUBRID_DATABASES/databases.txt $F_OPTION 2>&1)

if [ $? -eq 0 ]; then
    echo ".. OK"
else
    echo ".. Fail"
    echo ""
    echo "$RUN_COPY_TXT"
    exit 1
fi


# 복사한 databases.txt에서 ASIS DBNAME to TOBE DBMANE vol-path ,log-path 를 변경
# log-path는 임의의로 vol-path와 동일하게 설정한다.(log디렉토리 무시)

echo -n "EDIT databases.txt ."
cd $F_OPTION

file_path="databases.txt"
temp_file=$(mktemp)
lob_base_path="${F_OPTION}lob"

while IFS= read -r line; do
    if echo "$line" | grep -q "$ASIS_DBNAME"; then
        read -a fields <<< "$line"
        #fields[0]=$TOBE_DBNAME
        # 두번째, 네번째, 다섯번째 필드 수정
        fields[1]=$F_OPTION
        fields[3]=$F_OPTION
        fields[4]="file:$lob_base_path"

        echo "${fields[@]}" >> "$temp_file"
    else
        echo "$line" >> "$temp_file"
    fi
done < "$file_path"

# Overwrite temporary files with original files
mv "$temp_file" "$file_path"

#RUM_MOVE_TXT=$(mv "$TEMP_FILE" "$TOBE_FULLPATH/databases.txt" 2>&1)

if [ $? -eq 0 ]; then
    echo ".. OK"
else
    echo ".. FAIL"
    echo ""
    echo "$RUN_MOVE_TXT"
    exit 1
fi

# CHANGE CUBRID_DATABASES
echo -n "CHANGE CUBRID_DATABSES ..."
export CUBRID_DATABASES=$F_OPTION

if [ $? -eq 0 ]; then
    echo ".. OK"
else
    echo ".. FAIL"
    echo ""
    echo "CHANGE export"
    exit 1
fi
# $BACKUP_PATH경로에서 $F_OPTION PATH restoredb
cd $F_OPTION
#export CUBRID_DATABASES=$COPY_DB_VOL_PATH
#RUN_RESTORE_DBMS=`cubrid restoredb -B $BACKUP_PATH -u $ON_DB_NAME -o restoredb_$RUN_DATE.log`

# 복구 작업을 시작하면서 진행 상태 표시
echo -n "RUN RESTORE DB ...."
echo ""
RUN_DATE=`date '+%Y%m%d%H%M%S'`

# restoredb command
CMD="cubrid restoredb"

# Add options if set
#[[ -n "$F_OPTION" ]] && CMD+=" -F $F_OPTION"
[[ -n "$B_OPTION" ]] && CMD+=" -B $B_OPTION"
[[ -n "$UP_TO_DATE" ]] && CMD+=" -d $UP_TO_DATE"
[[ -n "$LEVEL" ]] && CMD+=" -l $LEVEL"
$PARTIAL_RECOVERY && CMD+=" -p"
[[ -n "$KYE_FILE_PATH" ]] && CMD+=" -k $KYE_FILE_PATH"

# Add mandatory arguments
#CMD+=" -u -o restoredb_$RUN_DATE.log $ASIS_DBNAME 2>&1"
CMD+=" -u -o restoredb_$RUN_DATE.log $ASIS_DBNAME"

# Execute command
#echo "Executing: $CMD &"
eval "$CMD &"

# 백그라운드에서 실행 중인 작업의 PID를 가져옴
CMD_PID=$!
#RESTORE_PID=$!

# 진행 상태 표시
echo $CUBRID_DATABASES

while kill -0 "$CMD_PID" 2>/dev/null; do
    echo -n "."
    sleep 1
done

# 명령어가 완료되었는지 확인
wait $CMD_PID
RESTORE_EXIT_STATUS=$?

if [ $RESTORE_EXIT_STATUS -eq 0 ]; then
    echo ".... OK in ${SECONDS}s"
else
    echo ".... Fail"
    echo ""
    echo "$RESTORE DBMS"
    exit 1
fi



# renamedb asis tobe
echo -n "RENAMEDB ..."
RENAME_DBMS=$(cubrid renamedb $ASIS_DBNAME $TOBE_DBNAME 2>&1)

if [ $? -eq 0 ]; then
    echo ".. OK"
else
    echo ".. FAIL"
    echo ""
    exit 1
fi

# Rollback
echo "ROLLBACK CUBRID_DATABASES ..."
export CUBRID_DATABASES=$ASIS_CUBRID_DATABASES
echo ".. OK"

# Add asis databases.txt
#echo "===="
#echo $F_OPTION
#echo $file_path
new_path=$ASIS_CUBRID_DATABASES/databases.txt

# Check file_path exists
if [ ! -f "$file_path" ]; then
    echo "Error: $file_path not found in $F_OPTION"
    exit 1
fi

# Find line for TOBE_NAME
search_line=$(grep -n "^$TOBE_DBNAME" "$file_path" | cut -d: -f1)
temp_file="./temp.txt"

if [ -z "$search_line" ]; then
    echo "Error: The specified line with $TOBE_NAME not found in $file_path"
else
    echo "Find line ... OK"
    #echo "Line found: $search_line"
fi

> "$temp_file"

content=$(sed -n "${search_line}p" "$file_path")

cat "$new_path" > "$temp_file"
echo "$content" >> "$temp_file"

mv "$temp_file" "$new_path"


# Rollback
#echo "ROLLBACK CUBRID_DATABASES ..."
#export CUBRID_DATABASES=$ASIS_CUBRID_DATABASES

echo ""
echo "FINISH COPY_DBMS!!."
