select * FROM db_drop_function('psp_%s');
create or replace function psp_%s (_pgasp_GET_ varchar, CU integer, _argv_ text[])
returns %s as $$
<<%s>>
declare
    clock_start timestamptz := clock_timestamp();
    _pgasp_ text;
    _p_ varchar[];
    _n_ integer := 1;
    v_error_source text           := 'psp_' || $q$%s$q$::text;
    v_error_pgasp_get text        := _pgasp_GET_;
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
