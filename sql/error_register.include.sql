CREATE TABLE IF NOT EXISTS _error_register
(
  id bigint,
  error_severity text,
  error_source text,
  error_source_type text,
  error_parameters text,
  user_id text,
  user_email text,
  sqlstate text,
  column_name text,
  constraint_name text,
  pg_datatype_name text,
  message_text text,
  table_name text,
  schema_name text,
  error_detail text,
  error_hint text,
  error_context text,
  creation_timestamp timestamp with time zone,
  error_timestamp timestamp with time zone,
  additional_information text,
  is_processed boolean DEFAULT false
);
CREATE SEQUENCE IF NOT EXISTS _error_seq
  INCREMENT 1
  MINVALUE 1
  MAXVALUE 9223372036854775807
  START 1
  CACHE 1;
