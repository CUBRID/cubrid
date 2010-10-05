<?php
echo "<html>";
echo "<title>cubrid_result</title>";
echo "PHP version: ".phpversion();
echo "<br>";
echo "<br>";
echo "Testing <b>cubrid_result</b> implementation...";
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

  echo "<br>";
  echo "Testing <b>cubrid_result</b>...<br>";
  echo "<br>";

  $start_time = gettimeofday(true);

  $result = cubrid_execute($cubrid_con, $sql);

  $end_time = gettimeofday(true);

  if ($result) 
  {
    $value = cubrid_result($result, 0); 
    echo "Value returned (Row=0,Col not specified - assumed 0):";
    echo $value;
    echo "<br>";
    echo "Type returned: ".gettype($value);
    echo "<br>";
  
    echo "<br>";
    $value = cubrid_result($result, 0, 0); 
    echo "Value returned (Row=0,Col=0):";
    echo $value;
    echo "<br>";
    echo "Type returned: ".gettype($value);
    echo "<br>";
  
    echo "<br>";
    $value = cubrid_result($result, 2, 2); 
    echo "Value returned (Row=2,Col=2):";
    echo $value;
    echo "<br>";
    echo "Type returned: ".gettype($value);
    echo "<br>";
  
    echo "<br>";
    $value = cubrid_result($result, 2, "column_integer"); 
    echo "Value returned (Row=2,Col='column_integer'):";
    echo $value;
    echo "<br>";
    echo "Type returned: ".gettype($value);
    echo "<br>";
  
    cubrid_close_request($result); 
  }


  $exec_time = $end_time-$start_time;
  echo "<br>"."SQL execution completed in: ".$exec_time." sec."."<br>";
  echo "<br>";


  echo "<b>Testing cubrid_result with wrong number of params: 0</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    cubrid_result(); 
    cubrid_close_request($result); 
  }

  echo "<br>";
  echo "<b>Testing cubrid_result with wrong number of params: 4</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    cubrid_result(1,1,1,1); 
    cubrid_close_request($result); 
  }

  echo "<br>";
  echo "<b>Testing cubrid_result with wrong row offset: 99</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    cubrid_result($result, 99, 0); 
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
