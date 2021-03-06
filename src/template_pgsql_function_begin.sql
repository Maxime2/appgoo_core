select * FROM db_drop_function('ag_%s');
create or replace function ag_%s (_ag_GET_ varchar, CU bigint, _argv_ text[])
returns %s as $$
<<%s>>
declare
    clock_start timestamptz := clock_timestamp();
    _ag_ text;
    _p_ varchar[];
    _n_ integer := 1;
    v_error_source text           := 'ag_' || $q$%s$q$::text;
    v_error_ag_get text           := _ag_GET_;
    v_error_CU text               := CU::text;
    v_error_severity text         := 'ERROR';
    v_error_sqlstate text         := '';
    v_error_column_name text      := '';
    v_error_constraint_name text  := '';
    v_error_pg_datatype_name text := '';
    v_error_message_text text     := '';
    v_error_table_name text       := '';
    v_error_schema_name text      := '';
    v_error_detail text           := '';
    v_error_hint text             := '';
    v_error_context text          := '';
    v_errorinfo text              := '';
