<?php
echo "<html>";
echo "<title>cubrid_field_len</title>";
echo "PHP version: ".phpversion();
echo "<br>";
echo "<br>";
echo "Testing <b>cubrid_field_len</b> implementation...";
echo "<br>"."<br>";

require_once('connect.inc.php');

$sql = "select * from test_table";
//TODO Determine correct values that will be returned
$expected_lens = array(10, 5, 11, 9, 92, 10, 4, 8, 23, 1073741823);

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

  echo "Testing <b>cubrid_field_len</b>...<br>";

  $start_time = gettimeofday(true);

  $result = cubrid_execute($cubrid_con, $sql);

  $end_time = gettimeofday(true);

  if ($result) 
  {
    $num_columns = cubrid_num_cols($result);
    $columns = cubrid_column_names($result);
//    $num_columns = 10;
    //$columns = array("","","","","","","","","","");


    echo "<br>";
    echo "<table border='1'>";
    echo "<tr><td><b>Column name</b></td><td><b>Reported Column length</b></td><td><b>Expected Column length</b></td></tr>";
    for ($i = 0; $i < $num_columns; $i++) 
    {
      $len = cubrid_field_len($result, $i);
      echo "<tr><td>".$columns[$i]."</td><td>".$len."</td><td>".$expected_lens[$i]."</td></tr>";
    }
    echo "</table>";

    cubrid_close_request($result); 
  }


  $exec_time = $end_time-$start_time;
  echo "<br>"."SQL execution completed in: ".$exec_time." sec."."<br>";

  echo "<br>";


  echo "<br>";
  echo "<b>Testing cubrid_field_len with wrong number of params: 0</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    $lens = cubrid_field_len();
    cubrid_close_request($result); 
  }
  echo "<br>";

  echo "<br>";
  echo "<b>Testing cubrid_field_len with wrong number of params: 3</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    $lens = cubrid_field_len(1,1,1);
    cubrid_close_request($result); 
  }
  echo "<br>";


  echo "<br>";
  echo "<b>Testing cubrid_field_len with wrong column index: 99</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    $lens = cubrid_field_len($result, 99);
    cubrid_close_request($result); 
  }
  echo "<br>";


  cubrid_disconnect($cubrid_con);
  echo "Connection closed."."<br>";
}

echo "<br>";
echo "SQL Script used to create database objects used for testing: "."<a href='db_script.php'>View</a>";
echo "<br>";
echo "</html>";
?>
