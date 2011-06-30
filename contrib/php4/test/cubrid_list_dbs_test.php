<?php
echo "<html>";
echo "<title>cubrid_list_dbs</title>";
echo "PHP version: ".phpversion();
echo "<br>";
echo "<br>";
echo "Testing <b>cubrid_list_dbs</b> implementation...";
echo "<br>"."<br>";
require_once('connect.inc.php');

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
  echo "Testing <b>cubrid_list_dbs with 1-parameter</b>...<br>";
  echo "<br>";

  $dbs = cubrid_list_dbs($cubrid_con);
  echo "Databases: <br>";
  echo var_dump($dbs);
  echo "<br>";

  echo "<br>";
  echo "<b>Testing cubrid_list_dbs with wrong number of params: 0</b>...<br>";
  cubrid_list_dbs();
  echo "<br>";

  echo "<br>";
  echo "<b>Testing cubrid_list_dbs with wrong number of params: 2</b>...<br>";
  cubrid_list_dbs(1,1);
  echo "<br>";


  cubrid_disconnect($cubrid_con);
  echo "Connection closed."."<br>";
}

echo "<br>";
echo "SQL Script used to create database objects used for testing: "."<a href='db_script.php'>View</a>";
echo "<br>";
echo "</html>";
?>
