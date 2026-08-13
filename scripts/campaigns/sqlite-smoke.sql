.bail on
.echo off
.headers off
.mode list
PRAGMA foreign_keys = ON;
CREATE TABLE department(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE);
CREATE TABLE employee(id INTEGER PRIMARY KEY, department_id INTEGER NOT NULL REFERENCES department(id), name TEXT NOT NULL, salary INTEGER NOT NULL CHECK(salary >= 0));
INSERT INTO department VALUES(1, 'compiler');
INSERT INTO department VALUES(2, 'runtime');
INSERT INTO department VALUES(3, 'tools');
INSERT INTO employee VALUES(1, 1, 'Ada', 120);
INSERT INTO employee VALUES(2, 1, 'Ken', 100);
INSERT INTO employee VALUES(3, 2, 'Dennis', 110);
INSERT INTO employee VALUES(4, 3, 'Margaret', 130);
CREATE INDEX employee_department ON employee(department_id);
SELECT name FROM employee ORDER BY id;
SELECT d.name, count(*), sum(e.salary) FROM department AS d JOIN employee AS e ON e.department_id = d.id GROUP BY d.id ORDER BY d.id;
UPDATE employee SET salary = salary + 5 WHERE department_id = 1;
SELECT name, salary FROM employee WHERE salary >= 110 ORDER BY salary DESC, name;
DELETE FROM employee WHERE name = 'Ken';
SELECT count(*), min(salary), max(salary) FROM employee;
BEGIN IMMEDIATE;
INSERT INTO employee VALUES(5, 2, 'Frances', 125);
SAVEPOINT campaign_savepoint;
UPDATE employee SET salary = 999 WHERE id = 5;
ROLLBACK TO campaign_savepoint;
RELEASE campaign_savepoint;
COMMIT;
WITH RECURSIVE seq(n) AS (VALUES(1) UNION ALL SELECT n + 1 FROM seq WHERE n < 5) SELECT group_concat(n, ':') FROM seq;
CREATE VIEW payroll AS SELECT d.name AS department, sum(e.salary) AS total FROM department AS d JOIN employee AS e ON e.department_id = d.id GROUP BY d.id;
SELECT department, total FROM payroll ORDER BY department;
DROP VIEW payroll;
ANALYZE;
REINDEX employee_department;
VACUUM;
PRAGMA integrity_check;
