/*
  urlencode for PL/PGSQL
 */

CREATE OR REPLACE FUNCTION db_urlencode(in_str text)
 RETURNS text AS
$body$
DECLARE
    _i      int4;
    _temp   varchar;
    _ascii  int4;
    _result varchar;
BEGIN
    _result = '';
    FOR _i IN 1 .. length(in_str) LOOP
        _temp := substr(in_str, _i, 1);
        IF _temp ~ '[0-9a-zA-Z:/@._?#-]+' THEN
            _result := _result || _temp;
        ELSE
            _ascii := ascii(_temp);
            IF _ascii > x'07ff'::int4 THEN
                RAISE EXCEPTION 'Won''t deal with 3 (or more) byte sequences.';
            END IF;
            IF _ascii <= x'0f'::int4 THEN
                _temp := '%0'||to_hex(_ascii);
            ELSIF _ascii <= x'07f'::int4 THEN
                _temp := '%'||to_hex(_ascii);
            ELSE
                _temp := '%'||to_hex((_ascii & x'03f'::int4)+x'80'::int4);
                _ascii := _ascii >> 6;
                _temp := '%'||to_hex((_ascii & x'01f'::int4)+x'c0'::int4)
                            ||_temp;
            END IF;
            _result := _result || upper(_temp);
        END IF;
    END LOOP;
    RETURN _result;
    
EXCEPTION
    WHEN OTHERS THEN
        return in_str;
END
$body$
LANGUAGE plpgsql STABLE;
