/* db_drop_function.include.sql

   db_drop_function. Author. Date
    Information about this function's purpose and any special notes on how best to use it

    TO-DO
    ===============================================
    * Major enhancement still to perform
    * Major enhancement still to perform...

    MAJOR UPDATES
    ===============================================
    Date. Who. If the function was in use but has now been enhanced, then mention this here.

*/

CREATE OR REPLACE FUNCTION db_drop_function(
    p_fn_name text default ''
    )
  RETURNS VOID AS
$BODY$
declare

    _errorinfo              text := ''; -- You can use this variable to pass specific information to the error register
    _error_source           text := 'db_drop_function';
    _error_parameters       text := 'p_fn_name: ' || p_fn_name::text;
    _error_CU               text := 0::text;   -- update assignment if parameter p_CU is not used
    _error_severity         text := ''; -- you can optionally populate this LOW/HIGH or WARNING/ERROR
    _error_sqlstate         text;
    _error_column_name      text;
    _error_constraint_name  text;
    _error_pg_datatype_name text;
    _error_message_text     text;
    _error_table_name       text;
    _error_schema_name      text;
    _error_detail           text;
    _error_hint             text;
    _error_context          text;

    funcrow RECORD;
    numfunctions smallint := 0;
    numparameters int;
    i int;
    paramtext text;
    
begin

    /* _errorinfo := debug information about the function;  */

FOR funcrow IN SELECT p.proargtypes, n.nspname FROM pg_proc p, pg_namespace n
               WHERE p.proname = p_fn_name AND n.oid = p.pronamespace
	         AND n.nspname in ('public', 'jesla')
LOOP

    --for some reason array_upper is off by one for the oidvector type, hence the +1
    numparameters = array_upper(funcrow.proargtypes, 1) + 1;

    i = 0;
    paramtext = '';

    LOOP
        IF i < numparameters THEN
            IF i > 0 THEN
                paramtext = paramtext || ', ';
            END IF;
            paramtext = paramtext || (SELECT typname FROM pg_type WHERE oid = funcrow.proargtypes[i]);
            i = i + 1;
        ELSE
            EXIT;
        END IF;
    END LOOP;

    EXECUTE 'DROP FUNCTION ' || funcrow.nspname || '.' || p_fn_name || '(' || paramtext || ');';
    numfunctions = numfunctions + 1;

END LOOP;

IF numfunctions > 1 THEN
   perform db_log('Dropped ' || numfunctions::text || ' functions ' || p_fn_name);
END IF;


EXCEPTION
    WHEN OTHERS THEN
        get stacked diagnostics
            _error_sqlstate = returned_sqlstate,
            _error_column_name = column_name,
            _error_constraint_name = constraint_name,
            _error_pg_datatype_name = pg_datatype_name,
            _error_message_text = message_text,
            _error_table_name = table_name,
            _error_schema_name = schema_name,
            _error_detail = pg_exception_detail,
            _error_hint = pg_exception_hint,
            _error_context = pg_exception_context;

        execute db_error_register(
            _error_source,
            'function',
            _error_parameters,
            _error_severity,
            _error_CU,
            _error_sqlstate,
            _error_column_name,
            _error_constraint_name,
            _error_pg_datatype_name,
            _error_message_text,
            _error_table_name,
            _error_schema_name,
            _error_detail,
            _error_hint,
            _error_context,
            now()::timestamptz,
            _errorinfo
            );

        /* put your return logic here and consider the consequences of downstream functions who
            are not aware that the handled error has occurred
        */

end;
$BODY$
  LANGUAGE plpgsql VOLATILE
  COST 100;
