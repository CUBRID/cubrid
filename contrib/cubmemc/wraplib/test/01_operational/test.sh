#!/bin/bash
source ../env.sh

database_name="testdb01"
result_dir="result"
answer_dir="answer"

schema_template="schema.sql.template"
test_sql="test.sql"

schema_generated="$result_dir/schema.sql"
result_stdout="$result_dir/result.stdout"
result_stderr="$result_dir/result.stderr"
answer_stdout="$answer_dir/result.stdout"
answer_stderr="$answer_dir/result.stderr"
run_log="$result_dir/run.log"

## setup
rm -rf $result_dir/*
check_error "Failed to remove the contents of $result_dir"
touch $run_log

sed -e "s|##server_list##|$server_list|" -e "s|##behavior##|$behavior|" -e "s|##libcubmemc_path##|$libcubmemc_path|" $schema_template > $schema_generated
check_error "Failed to generate $schema_generated from $schema_template"

cubrid createdb $database_name 2>&1 >> $run_log
check_error "Failed to create database $database_name"

csql -S -i $schema_generated $database_name 2>&1 >> $run_log
check_error "Failed to generate schema"

cubrid server start $database_name 2>&1 >> $run_log
check_error "Failed to start database $database_name"

## run test
csql -e -i $test_sql $database_name 1>"$result_stdout" 2>"$result_stderr"; true

## tear down
cubrid server stop $database_name 2>&1 >> $run_log; true
cubrid deletedb $database_name 2>&1 >> $run_log; true
rm -rf csql.err

## check result
diff $result_stdout $answer_stdout > $result_dir/diff.stdout
diff $result_stderr $answer_stderr > $result_dir/diff.stderr
cat $result_dir/diff.stdout $result_dir/diff.stderr | wc -l
