DROP TABLE test_table;

CREATE TABLE test_table(column_integer INTEGER,
column_smallint SMALLINT,
column_numeric_9_2 NUMERIC(9,2),
column_char_9 CHAR(9),
column_varchar_92 VARCHAR(92),
column_date DATE,
column_bit BIT(4),
column_time TIME,
column_timestamp TIMESTAMP,
column_set SET,
PRIMARY KEY (column_integer)
);

DELETE FROM test_table;

INSERT INTO test_table VALUES(1, 11, 1.1, '1', CURRENT_USER, SYS_DATE, NULL, SYS_TIME, SYS_TIMESTAMP, NULL);
INSERT INTO test_table VALUES(22, 222, 2.2, '22', CURRENT_USER, SYS_DATE, NULL, SYS_TIME, SYS_TIMESTAMP, {1});
INSERT INTO test_table VALUES(333, 3333, 3.3, '333', CURRENT_USER, SYS_DATE, NULL, SYS_TIME, SYS_TIMESTAMP, {1,2});

DROP TABLE test_table_2;

CREATE TABLE test_table_2(column_integer INTEGER,
column_serial  INTEGER auto_increment);
