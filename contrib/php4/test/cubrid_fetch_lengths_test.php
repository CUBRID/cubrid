<?php
echo "<html>";
echo "<title>cubrid_fetch_lengths</title>";
echo "PHP version: ".phpversion();
echo "<br>";
echo "<br>";
echo "Testing <b>cubrid_fetch_lengths</b> implementation...";
echo "<br>"."<br>";

require_once('connect.inc.php');

$sql = "select * from test_table";
$expected_lens = array(1, 2, 4, 9, 3, 8, 8, 7, 16, 16);

echo "Connecting to the database..."."<br>";

$cubrid_con = cubrid_connect($host_ip, $host_port, $cubrid_name, $cubrid_user, $cubrid_password);

if (!$cubrid_con) 
{
  echo "Error code: ".cubrid_error_code().". Message: ".cubrid_error_msg()."<br>";
} 
else 
{
  echo "Connected."."<br>";

  echo "Executing: <b>".$sql."</b><br>";

  echo "Testing <b>cubrid_fetch_lengths</b>...<br>";

  $start_time = gettimeofday(true);

  $result = cubrid_execute($cubrid_con, $sql);

  $end_time = gettimeofday(true);

  if ($result) 
  {
    $num_columns = cubrid_num_cols($result);

    $row = cubrid_fetch($result);
    echo "<br>";
    echo "Returned type: ".gettype($row);
    echo "<br>";
    echo "Fetched row: ";
    for ($i = 0; $i < $num_columns; $i++) 
    {
      echo "[".$row[$i]."], ";
    }
    echo "<br>";

    $lens = cubrid_fetch_lengths($result);
    echo "Fetched lengths: ";
    for ($i = 0; $i < $num_columns; $i++) 
    {
      echo "[".$lens[$i]."], ";
    }
    echo "<br>";

    cubrid_close_request($result); 
  }

  $exec_time = $end_time-$start_time;
  echo "<br>"."SQL execution completed in: ".$exec_time." sec."."<br>"."<br>";


  echo "<br>";
  echo "<b>Testing cubrid_fetch_lengths with wrong number of params: 0</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    $lens = cubrid_fetch_lengths();
    cubrid_close_request($result); 
  }
  echo "<br>";

  echo "<br>";
  echo "<b>Testing cubrid_fetch_lengths with wrong number of params: 2</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    $lens = cubrid_fetch_lengths(1,1);
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
