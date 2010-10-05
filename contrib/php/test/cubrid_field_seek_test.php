<?php
echo "<html>";
echo "<title>cubrid_field_seek</title>";
echo "PHP version: ".phpversion();
echo "<br>";
echo "<br>";
echo "Testing <b>cubrid_field_seek</b> implementation...";
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

  echo "Testing <b>cubrid_field_seek</b>...<br>";

  $start_time = gettimeofday(true);

  $result = cubrid_execute($cubrid_con, $sql);

  $end_time = gettimeofday(true);

  if ($result) 
  {
    cubrid_field_seek($result, 0);
    $val = cubrid_fetch_field($result); 
    echo "<br>";
    echo "[Column 0]: Type returned: ".gettype($val);
    echo "<br>";
    echo "[Column 0]: ".var_dump($val);
    echo "<br>";
    echo "[Column 0]: Name = ".$val->name;
    echo "<br>";
    cubrid_field_seek($result, 4);
    $val = cubrid_fetch_field($result); 
    echo "<br>";
    echo "[Column 4]: Type returned: ".gettype($val);
    echo "<br>";
    echo "[Column 4]: ".var_dump($val);
    echo "<br>";
    echo "[Column 4]: Name = ".$val->name;
    echo "<br>";

    cubrid_close_request($result); 
  }

  $exec_time = $end_time-$start_time;
  echo "<br>"."SQL execution completed in: ".$exec_time." sec."."<br>";
  echo "<br>";

  echo "<b>Testing cubrid_field_seek with wrong number of params: 0</b>...<br>";

  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    cubrid_field_seek();
    cubrid_close_request($result); 
  }
  echo "<br>";


  echo "<br>";
  echo "<b>Testing cubrid_field_seek with wrong number of params: 3</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    cubrid_field_seek(1,1,1);
    cubrid_close_request($result); 
  }
  echo "<br>";

  echo "<br>";
  echo "<b>Testing cubrid_field_seek with wrong offset: 99</b>...<br>";
  $result = cubrid_execute($cubrid_con, $sql);
  if ($result) 
  {
    cubrid_field_seek(99);
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
