/*
  ag parse get functions for PL/PGSQL
 */

create or replace function ag_parse_get (p_get varchar, p_param varchar, p_default varchar default '')
returns varchar as
$body$
declare
     v_get   varchar := '&' || p_get   || '&';
     v_param varchar := '&' || p_param || '=';
     x       integer;
begin

     x = position (v_param in v_get);
     if x = 0 then return p_default; end if;

     v_get := substring (v_get from x+length(v_param));
     x = position ('&' in v_get);
     if x = 1 then return p_default; end if;

     v_get := substring (v_get from 1 for x-1);

     return db_urldecode(v_get);

end;
$body$
  language plpgsql STABLE
  COST 100;

--
create or replace function ag_parse_get_date
(
     p_get varchar,
     p_param varchar,
     p_default varchar default ''
)
returns date as
$body$
declare
     v varchar := ag_parse_get(p_get, p_param, p_default);
     d date;
begin

     begin
            d := v::date;
     exception when others then
            d := null::date;
     end;

     return d;

end;
$body$
  language plpgsql STABLE
  COST 100;

--
create or replace function ag_parse_get_timestamptz
(
     p_get varchar,
     p_param varchar,
     p_default varchar default ''
)
returns timestamptz as
$body$
declare
     v varchar := ag_parse_get(p_get, p_param, p_default);
     d timestamptz;
begin

     begin
            d := v::timestamptz;
     exception when others then
            d := null::timestamptz;
     end;

     return d;

end;
$body$
  language plpgsql STABLE
  COST 100;

--
create or replace function ag_parse_get_json
(
     p_get varchar,
     p_param varchar,
     p_default varchar default ''
)
returns json as
$body$
declare
     v varchar := ag_parse_get(p_get,p_param,p_default);
     j json;
begin

    begin
        j := v::json;
    exception when others then
        j := null::json;
    end;

    return j;

end;
$body$
  language plpgsql STABLE
  COST 100;
