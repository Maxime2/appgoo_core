/*
  urldecode for PL/PGSQL. This SQL Function will convert the input of 'Hello+good-bye+%2B+%26+a+kiss'
  to an output of 'Hello good-bye + & a kiss'
 */

CREATE OR REPLACE FUNCTION db_urldecode(
    p_encoded text DEFAULT '')
  RETURNS text AS
$BODY$

  SELECT convert_from(CAST(E'\\x' || array_to_string(ARRAY(
    SELECT CASE WHEN length(r.m[1]) = 1 THEN 
      encode(convert_to(r.m[1], 'SQL_ASCII'), 'hex') 
    ELSE 
      substring(r.m[1] from 2 for 2) 
    END
  FROM regexp_matches(replace($1, '+', ' '), '%[0-9a-f][0-9a-f]|.', 'gi') AS r(m)), '') AS bytea), 'UTF8')

$BODY$
  LANGUAGE sql STABLE
  COST 100;

