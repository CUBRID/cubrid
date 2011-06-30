<?php
echo "<html>";
echo "<title>cubrid_insert_id</title>";
echo "PHP version: ".phpversion();
echo "<br>";
echo "<br>";
echo "Testing <b>cubrid_insert_id</b> implementation...";
echo "<br>"."<br>";

require_once('connect.inc.php');

$sql = "insert into test_table_2(column_integer) values(".rand().")";
$table = "test_table_2";

echo "Connecting to the database..."."<br>";

$cubrid_con = @cubrid_connect($host_ip, $host_port, $cubrid_name, $cubrid_user, $cubrid_password);

if (!$cubrid_con) 
{
  echo "Error code: ".cubrid_error_code().". Message: ".cubrid_error_msg()."<br>";
} 
else 
{
  echo "Connected.<br><br>";

  echo "Testing <b>cubrid_insert_id</b>...<br>";
  echo "<br>";
  echo "Executing: <b>".$sql."</b><br>";
  echo "<br>";

  $result = cubrid_execute($cubrid_con, $sql, CUBRID_INCLUDE_OID);
  cubrid_commit ($cubrid_con); 

  if ($result) 
  {
    echo "Getting auto increment values used in INSERT...";
    echo "<br>";
    echo "Testing <b>cubrid_insert_id</b> with <b>1</b> parameter...<br>";
    echo "<br>";
    $value = cubrid_insert_id($table); 
    echo "Values returned:<br>";
    echo var_dump($value);
    echo "<br>";
    echo "Testing <b>cubrid_insert_id</b> with <b>2</b> parameters...<br>";
    echo "<br>";
    $value = cubrid_insert_id($cubrid_con, $table); 
    echo "Values returned:<br>";
    echo var_dump($value);
    echo "<br>";
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
