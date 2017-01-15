/* db_error_register.sql

    Use this function to register any errors encountered. It does not return anything
*/

-- Do not forget to call these functions before you include this file!

-- perform db_drop_function('_db_error_register');
-- perform db_drop_function('db_error_register');
-- perform db_drop_function('db_error_register_serial');


CREATE OR REPLACE FUNCTION _db_error_register(
    p_background boolean default TRUE,
    p_error_source text default '',
    p_error_source_type text default '',   -- psp_page, function, syslog
    p_error_parameters text default '',
    p_error_severity text default 'ERROR',
    p_error_CU text default '0',
    p_error_sqlstate text default '',
    p_error_column_name text default '',
    p_error_constraint_name text default '',
    p_error_pg_datatype_name text default '',
    p_error_message_text text default '',
    p_error_table_name text default '',
    p_error_schema_name text default '',
    p_error_detail text default '',
    p_error_hint text default '',
    p_error_context text default '',
    p_timestamptz timestamptz default now(),
    p_additional_information text default ''
    )
  RETURNS VOID AS
$BODY$
declare

    _err_text text := '';
    v_error_severity text := (case when p_error_severity = '' then 'ERROR' else p_error_severity end);
    v_SQL text;

begin

    /* old
    select nextval(''' || '_error_seq'''       || '),
    The sequence is now assigned by a pre-insert trigger if the error is to be logged/kept and not excluded
    */

    v_SQL := 'insert into _error_register
        (id, error_source, error_source_type, error_parameters, error_severity, user_id, user_email,
            sqlstate, column_name, constraint_name, pg_datatype_name, message_text,
            table_name, schema_name, error_detail, error_hint, error_context,
            creation_timestamp, error_timestamp, additional_information)
        select 0,
        $error$' || coalesce((p_error_source), '#NULL')                     || '$error$,
        $error$' || coalesce((p_error_source_type), '#NULL')                || '$error$,
        $error$' || coalesce((p_error_parameters), '#NULL')                 || '$error$,
        $error$' || coalesce((v_error_severity), '#NULL')                   || '$error$,
        $error$' || db_cast(0::integer, p_error_CU, 0) || '$error$,
        $error$' || (case when (select length(coalesce(notification_email, username)) from users where id = db_cast(0::integer, p_error_CU, 0)) > 1
                 then (select coalesce(notification_email, username) from users where id = db_cast(0::integer, p_error_CU, 0))
                 else p_error_CU end)              || '$error$,
        $error$' || coalesce((p_error_sqlstate), '#NULL')                   || '$error$,
        $error$' || coalesce((p_error_column_name), '#NULL')                || '$error$,
        $error$' || coalesce((p_error_constraint_name), '#NULL')            || '$error$,
        $error$' || coalesce((p_error_pg_datatype_name), '#NULL')           || '$error$,
        $error$' || coalesce((p_error_message_text), '#NULL')               || '$error$,
        $error$' || coalesce((p_error_table_name), '#NULL')                 || '$error$,
        $error$' || coalesce((p_error_schema_name), '#NULL')                || '$error$,
        $error$' || coalesce((p_error_detail), '#NULL')                     || '$error$,
        $error$' || coalesce((p_error_hint), '#NULL')                       || '$error$,
        $error$' || coalesce((p_error_context), '#NULL')                    || '$error$,
        $error$' || db_clean(p_timestamptz)                      || '$error$,
        $error$' || db_clean(p_timestamptz)                      || '$error$,
        $error$' || coalesce((p_additional_information), '#NULL')           || '$error$;'
        ;

    if p_background then
        perform sh_psql_bg(v_SQL);
    else
        execute v_SQL;
    end if;



EXCEPTION
    WHEN OTHERS THEN
        -- As an error has occurred in the error register itself, we need to perform a very safe insert
        -- and identify it by prefixing the error source type with db_error_register_err_

        get stacked diagnostics
            _err_text = message_text;

        insert into _error_register
        (id, error_source, error_source_type, error_parameters, error_severity, user_id, user_email,
            sqlstate, column_name, constraint_name, pg_datatype_name, message_text,
            table_name, schema_name, error_detail, error_hint, error_context,
            creation_timestamp, error_timestamp, additional_information )
        select nextval('_error_seq'), p_error_source, '_db_error_register_' || p_error_source_type, p_error_parameters, 'ERROR',
        p_error_CU, '_unknown_',
        p_error_sqlstate, p_error_column_name, p_error_constraint_name, p_error_pg_datatype_name,
        p_error_message_text, p_error_table_name, p_error_schema_name, p_error_detail,
        p_error_hint, p_error_context, p_timestamptz, p_timestamptz,
        coalesce(_err_text, '@NULL') || ': ' || coalesce(p_additional_information, '#NULL') || ' v_SQL=' || coalesce(v_SQL, '<NULL>');

        RAISE NOTICE '_db_error_register error: % | % | % | %', p_error_source, p_error_source_type, p_error_message_text, p_error_parameters;

end;
$BODY$
  LANGUAGE plpgsql VOLATILE
  COST 100;



CREATE OR REPLACE FUNCTION db_error_register(
    p_error_source text default '',
    p_error_source_type text default '',   -- psp_page, function, syslog
    p_error_parameters text default '',
    p_error_severity text default 'ERROR',
    p_error_CU text default '0',
    p_error_sqlstate text default '',
    p_error_column_name text default '',
    p_error_constraint_name text default '',
    p_error_pg_datatype_name text default '',
    p_error_message_text text default '',
    p_error_table_name text default '',
    p_error_schema_name text default '',
    p_error_detail text default '',
    p_error_hint text default '',
    p_error_context text default '',
    p_timestamptz timestamptz default now(),
    p_additional_information text default ''
    )
  RETURNS VOID AS
$BODY$
declare

    _err_text text := '';
    v_error_severity text := (case when p_error_severity = '' then 'ERROR' else p_error_severity end);
begin
    perform _db_error_register(TRUE::boolean,
        coalesce(p_error_source, ''),
    	coalesce(p_error_source_type, ''),
    	coalesce(p_error_parameters, ''),
    	coalesce(p_error_severity, ''),
    	coalesce(p_error_CU, ''),
    	coalesce(p_error_sqlstate, ''),
    	coalesce(p_error_column_name, ''),
    	coalesce(p_error_constraint_name, ''),
    	coalesce(p_error_pg_datatype_name, ''),
    	coalesce(p_error_message_text, ''),
    	coalesce(p_error_table_name, ''),
    	coalesce(p_error_schema_name, ''),
    	coalesce(p_error_detail, ''),
    	coalesce(p_error_hint, ''),
    	coalesce(p_error_context, ''),
    	now()::timestamptz,
    	coalesce(p_additional_information, '')
    );

EXCEPTION
    WHEN OTHERS THEN
        -- As an error has occurred in the error register itself, we need to perform a very safe insert
        -- and identify it by prefixing the error source type with db_error_register_err_

        get stacked diagnostics
            _err_text = message_text;

        insert into _error_register
        (id, error_source, error_source_type, error_parameters, error_severity, user_id, user_email,
            sqlstate, column_name, constraint_name, pg_datatype_name, message_text,
            table_name, schema_name, error_detail, error_hint, error_context,
            creation_timestamp, error_timestamp, additional_information )
        select nextval('_error_seq'), p_error_source, 'db_error_register_' || p_error_source_type, p_error_parameters, 'ERROR',
        p_error_CU, '_unknown_',
        p_error_sqlstate, p_error_column_name, p_error_constraint_name, p_error_pg_datatype_name,
        p_error_message_text, p_error_table_name, p_error_schema_name, p_error_detail,
        p_error_hint, p_error_context, p_timestamptz, p_timestamptz,
        coalesce(_err_text, '@NULL') || ': ' || coalesce(p_additional_information, '#NULL');

        RAISE NOTICE 'db_error_register error: % | % | % | %', p_error_source, p_error_source_type, p_error_message_text, p_error_parameters;

end;
$BODY$
  LANGUAGE plpgsql VOLATILE
  COST 100;


CREATE OR REPLACE FUNCTION db_error_register_serial(
    p_error_source text default '',
    p_error_source_type text default '',   -- psp_page, function, syslog
    p_error_parameters text default '',
    p_error_severity text default 'ERROR',
    p_error_CU text default '0',
    p_error_sqlstate text default '',
    p_error_column_name text default '',
    p_error_constraint_name text default '',
    p_error_pg_datatype_name text default '',
    p_error_message_text text default '',
    p_error_table_name text default '',
    p_error_schema_name text default '',
    p_error_detail text default '',
    p_error_hint text default '',
    p_error_context text default '',
    p_timestamptz timestamptz default now(),
    p_additional_information text default ''
    )
  RETURNS VOID AS
$BODY$
declare

    _err_text text := '';
    v_error_severity text := (case when p_error_severity = '' then 'ERROR' else p_error_severity end);
begin
    perform _db_error_register(FALSE::boolean,
        coalesce(p_error_source, ''),
        coalesce(p_error_source_type, ''),
        coalesce(p_error_parameters, ''),
        coalesce(p_error_severity, ''),
        coalesce(p_error_CU, ''),
        coalesce(p_error_sqlstate, ''),
        coalesce(p_error_column_name, ''),
        coalesce(p_error_constraint_name, ''),
        coalesce(p_error_pg_datatype_name, ''),
        coalesce(p_error_message_text, ''),
        coalesce(p_error_table_name, ''),
        coalesce(p_error_schema_name, ''),
        coalesce(p_error_detail, ''),
        coalesce(p_error_hint, ''),
        coalesce(p_error_context, ''),
        now()::timestamptz,
        coalesce(p_additional_information, '')
    );

EXCEPTION
    WHEN OTHERS THEN
        -- As an error has occurred in the error register itself, we need to perform a very safe insert
        -- and identify it by prefixing the error source type with db_error_register_err_

        get stacked diagnostics
            _err_text = message_text;

        insert into _error_register
        (id, error_source, error_source_type, error_parameters, error_severity, user_id, user_email,
            sqlstate, column_name, constraint_name, pg_datatype_name, message_text,
            table_name, schema_name, error_detail, error_hint, error_context,
            creation_timestamp, error_timestamp, additional_information )
        select nextval('_error_seq'), p_error_source, 'db_error_register_serial_' || p_error_source_type, p_error_parameters, 'ERROR',
        p_error_CU, '_unknown_',
        p_error_sqlstate, p_error_column_name, p_error_constraint_name, p_error_pg_datatype_name,
        p_error_message_text, p_error_table_name, p_error_schema_name, p_error_detail,
        p_error_hint, p_error_context, p_timestamptz, p_timestamptz,
        coalesce(_err_text, '@NULL') || ': ' || coalesce(p_additional_information, '#NULL');

        RAISE NOTICE 'db_error_register_serial error: % | % | % | %', p_error_source, p_error_source_type, p_error_message_text, p_error_parameters;

end;
$BODY$
  LANGUAGE plpgsql VOLATILE
  COST 100;
