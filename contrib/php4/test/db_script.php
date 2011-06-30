<?php
echo "<html>";
echo "<title>cubrid_field_len</title>";
echo "<br>";
echo "<b>SQL Script used to create the Cubrid database objects used for testing</b>:";
echo "<br>"."<br>";
echo "DROP TABLE test_table;"."<br>";
echo "<br>";
echo "CREATE TABLE test_table("."<br>";
echo "&nbsp;&nbsp;"."column_integer INTEGER,"."<br>";
echo "&nbsp;&nbsp;"."column_smallint SMALLINT,"."<br>";
echo "&nbsp;&nbsp;"."column_numeric_9_2 NUMERIC(9,2),"."<br>";
echo "&nbsp;&nbsp;"."column_char_9 CHAR(9),"."<br>";
echo "&nbsp;&nbsp;"."column_varchar_92 VARCHAR(92),"."<br>";
echo "&nbsp;&nbsp;"."column_date DATE,"."<br>";
echo "&nbsp;&nbsp;"."column_bit BIT(4),"."<br>";
echo "&nbsp;&nbsp;"."column_time TIME,"."<br>";
echo "&nbsp;&nbsp;"."column_timestamp TIMESTAMP,"."<br>";
echo "&nbsp;&nbsp;"."column_set SET,"."<br>";
echo "&nbsp;&nbsp;"."PRIMARY KEY (column_integer)"."<br>";
echo ");"."<br>";
echo "<br>";
echo "INSERT INTO test_table VALUES(1, 11, 1.1, '1', CURRENT_USER, SYS_DATE, NULL, SYS_TIME, SYS_TIMESTAMP, NULL);"."<br>";
echo "INSERT INTO test_table VALUES(22, 222, 2.2, '22', CURRENT_USER, SYS_DATE, NULL, SYS_TIME, SYS_TIMESTAMP, {1});"."<br>";
echo "INSERT INTO test_table VALUES(333, 3333, 3.3, '333', CURRENT_USER, SYS_DATE, NULL, SYS_TIME, SYS_TIMESTAMP, {1,2});"."<br>";
echo "<br>";
echo "<br>";
echo "DROP TABLE test_table_2;"."<br>";
echo "<br>";
echo "CREATE TABLE test_table_2(column_integer INTEGER,column_serial  INTEGER auto_increment);";

echo "<br>";
echo "</html>";
?>
