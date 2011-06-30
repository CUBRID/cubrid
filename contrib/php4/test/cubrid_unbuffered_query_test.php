<?php
echo "<html>";
echo "<title>cubrid_unbuffered_query</title>";
echo "PHP version: ".phpversion();
echo "<br>";
echo "<br>";
echo "Testing <b>cubrid_unbuffered_query</b> implementation...";
echo "<br>"."<br>";

require_once('connect.inc.php');

$sql = "select * from test_table";

echo "Connecting to the database..."."<br>";

$cubrid_con = @cubrid_connect($host_ip, $host_port, $cubrid_name, $cubrid_user, $cubrid_password);

if (!$cubrid_con) 
{
  echo "Error code: ".cubrid_error_code().". Message: ".cubrid_error_msg()."<br>";
} 
else 
{
  echo "Connected."."<br>";

  echo "Executing: <b>".$sql."</b><br>";

  echo "Testing <b>cubrid_unbuffered_query</b>...<br>";

  $start_time = gettimeofday(true);
  echo "running with 2 arguments (sql query and connection)<br>";
  $result = cubrid_unbuffered_query($sql, $cubrid_con);
  $end_time = gettimeofday(true);

  if ($result) 
  {
    $num_columns = cubrid_num_cols($result);

    echo "<br>";
    echo "No of columns returned by the query: ".$num_columns;
    echo "<br>";

    $row = cubrid_fetch($result, CUBRID_OBJECT); 

    echo "Returned type: ".gettype($row);
    echo "<br>";
    echo "Values returned:";
    echo "<br>";

    echo $row->column_integer;
    echo "<br>";
    echo $row->column_smallint;
    echo "<br>";
    echo $row->column_numeric_9_2;
    echo "<br>";
    echo $row->column_char_9;
    echo "<br>";
    echo $row->column_varchar_92;
    echo "<br>";
    echo $row->column_date;
    echo "<br>";
    echo $row->column_bit;
    echo "<br>";
    echo $row->column_time;
    echo "<br>";
    echo $row->column_timestamp;
    echo "<br>";
    echo $row->column_set;
    echo "<br>";    
  
    cubrid_close_request($result); 
  }


  $exec_time = $end_time-$start_time;
  echo "<br>"."SQL execution completed in: ".$exec_time." sec."."<br>";
  echo "<br>";
  
  $start_time = gettimeofday(true);
  echo "running with 1 argument (sql query only)<br>";
  $result = cubrid_unbuffered_query($sql, $cubrid_con);
  $end_time = gettimeofday(true);

  if ($result) 
  {
    $num_columns = cubrid_num_cols($result);

    echo "<br>";
    echo "No of columns returned by the query: ".$num_columns;
    echo "<br>";

    $row = cubrid_fetch($result, CUBRID_OBJECT); 

    echo "Returned type: ".gettype($row);
    echo "<br>";
    echo "Values returned:";
    echo "<br>";

    echo $row->column_integer;
    echo "<br>";
    echo $row->column_smallint;
    echo "<br>";
    echo $row->column_numeric_9_2;
    echo "<br>";
    echo $row->column_char_9;
    echo "<br>";
    echo $row->column_varchar_92;
    echo "<br>";
    echo $row->column_date;
    echo "<br>";
    echo $row->column_bit;
    echo "<br>";
    echo $row->column_time;
    echo "<br>";
    echo $row->column_timestamp;
    echo "<br>";
    echo $row->column_set;
    echo "<br>";    
  
    cubrid_close_request($result); 
  }


  $exec_time = $end_time-$start_time;
  echo "<br>"."SQL execution completed in: ".$exec_time." sec."."<br>";
  echo "<br>";


  echo "<br>";
  echo "<b>Testing cubrid_unbuffered_query with wrong number of params: 0</b>...<br>";
  cubrid_unbuffered_query();
  echo "<br>";


  echo "<br>";
  echo "<b>Testing cubrid_unbuffered_query with wrong number of params: 3</b>...<br>";
  cubrid_unbuffered_query(1, 1, 1);
  echo "<br>";


  cubrid_disconnect($cubrid_con);
  echo "Connection closed."."<br>";
}

echo "<br>";
echo "SQL Script used to create database objects used for testing: "."<a href='db_script.php'>View</a>";
echo "<br>";
echo "</html>";
?>
