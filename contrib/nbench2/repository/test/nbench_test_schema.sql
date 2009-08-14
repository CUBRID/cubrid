
CREATE TABLE rwe_pk (a int NOT NULL, b int);
CREATE TABLE rwe_npk (a int, b int);
CREATE TABLE rwe_stat(k VARCHAR, v INT);

ALTER TABLE rwe_pk ADD CONSTRAINT ipk_rwe_pk PRIMARY KEY (a);