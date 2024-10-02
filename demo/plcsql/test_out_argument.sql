CREATE OR REPLACE FUNCTION test_out(
  val OUT INTEGER
) RETURN INTEGER
IS
BEGIN
  val := 9;
  RETURN 2;
END;

SELECT 1, 2 INTO :a, :b;
CALL test_out (:b);
SELECT :b;
SELECT test_out (:b) INTO :a; -- error (OUT) FUNCTION
SELECT :a, :b;

CREATE OR REPLACE FUNCTION test_out2(
  val IN OUT INTEGER
) RETURN INTEGER
IS
BEGIN
  val := 1;
  RETURN 2;
END;

SELECT 1, 2 INTO :a, :b;
CALL test_out2 (:b);
SELECT :b;
SELECT test_out2 (:b) INTO :a; -- error (IN OUT) FUNCTION
SELECT :a, :b;

CREATE OR REPLACE PROCEDURE test_out3(
  val IN OUT INTEGER
)
IS
BEGIN
  val := 9;
END;

SELECT 1 INTO :a;
CALL test_out3 (:a);
SELECT :a;
SELECT test_out3 (:a); -- error (IN OUT) PROCEDURE
SELECT :a, :b;

SELECT test_out (code) FROM athlete; -- error
SELECT test_out2 (code) FROM athlete; -- error
SELECT test_out3 (code) FROM athlete; -- error