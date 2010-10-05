<?php
echo "<html>";
echo "<title>cubrid_real_escape_string</title>";
echo "PHP version: ".phpversion();
echo "<br>";
echo "<br>";
echo "Testing <b>cubrid_real_escape_string</b> implementation...";
echo "<br>"."<br>";

require_once('connect.inc.php');

$sql = "SELECT \abc\, 'ds' FROM db_root";

echo "Initial memory: ".number_format(memory_get_usage(), 0, '.', ',') . " bytes.";
echo "<br>";
echo "<br>";

echo "Connecting to the database..."."<br>";

$cubrid_con = @cubrid_connect($host_ip, $host_port, $cubrid_name, $cubrid_user, $cubrid_password);

if (!$cubrid_con) 
{
  echo "Error code: ".cubrid_error_code().". Message: ".cubrid_error_msg()."<br>";
} 
else 
{
  echo "Connected."."<br>";

  echo "<br>";
  echo "Testing <b>cubrid_real_escape_string with 1-parameter</b>...<br>";

  echo "<br>";
  echo "Original string: ".$sql;
  echo "<br>";
  echo "Length=".strlen($sql);
  echo "<br>";
  $escaped_sql = cubrid_real_escape_string($sql);
  echo "Escaped string: ".$escaped_sql;
  echo "<br>";
  echo "Length=".strlen($escaped_sql);
  echo "<br>";

  echo "<br>";
  echo "Testing <b>cubrid_real_escape_string with 2-parameters</b>...<br>";

  echo "<br>";
  echo "Original string: ".$sql;
  echo "<br>";
  echo "Length=".strlen($sql);
  echo "<br>";
  $escaped_sql = cubrid_real_escape_string($sql, $cubrid_con);
  echo "Escaped string: ".$escaped_sql;
  echo "<br>";
  echo "Length=".strlen($escaped_sql);
  echo "<br>";


  echo "<br>";
  echo "<b>Testing cubrid_real_escape_string with wrong number of params: 0</b>...<br>";
  cubrid_real_escape_string();
  echo "<br>";

  echo "<br>";
  echo "<b>Testing cubrid_real_escape_string with wrong number of params: 3</b>...<br>";
  cubrid_real_escape_string(1,1,1);
  echo "<br>";


  cubrid_disconnect($cubrid_con);
  echo "Connection closed."."<br>";
}


echo "<br>";
echo 'Peak memory: ' . number_format(memory_get_peak_usage(), 0, '.', ',') . " bytes.";
echo "<br>";
echo 'End memory: ' . number_format(memory_get_usage(), 0, '.', ',') . " bytes.";
echo "<br>";


echo "<br>";
echo "SQL Script used to create database objects used for testing: "."<a href='db_script.php'>View</a>";
echo "<br>";
echo "</html>";
?>
