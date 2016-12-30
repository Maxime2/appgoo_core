
/*
for i in 1 .. _n_ loop
    if _p_[i] is null then _p_[i] := ''; end if;
end loop;
*/

_pgasp_ := array_to_string(_p_, '', '');


return _pgasp_;



exception when others then
    get stacked diagnostics
    	v_error_sqlstate = returned_sqlstate,
    	v_error_column_name = column_name,
    	v_error_constraint_name = constraint_name,
    	v_error_pg_datatype_name = pg_datatype_name,
    	v_error_message_text = message_text,
    	v_error_table_name = table_name,
    	v_error_schema_name = schema_name,
    	v_error_detail = pg_exception_detail,
    	v_error_hint = pg_exception_hint,
    	v_error_context = pg_exception_context;

        perform db_error_register(
            v_error_source,
            'psp_page',
            v_error_pgasp_get,
            v_error_severity,
            v_error_CU,
            v_error_sqlstate,
            v_error_column_name,
            v_error_constraint_name,
            v_error_pg_datatype_name,
            v_error_message_text,
            v_error_table_name,
            v_error_schema_name,
            v_error_detail,
            v_error_hint,
            v_error_context,
            now()::timestamptz,
            v_errorinfo
        );


--    if _mode_ > 0 then
        return
            '<pre style="background-color:yellow; color:red; font-size:small; line-height:normal; width:1000px;">SQL ERROR: ' || v_error_sqlstate ||
            '<br>SQL MESSAGE: ' || v_error_message_text ||
            '<br>SQL DETAIL: ' || v_error_detail ||
            '<br>SQL HINT: ' || v_error_hint ||
            '<br>SCHEMA NAME: ' || v_error_schema_name ||
            '<br>TABLE NAME: ' || v_error_table_name ||
            '<br>COLUMN NAME: ' || v_error_column_name ||
            '<br>CONSTRAINT NAME: ' || v_error_constraint_name ||
            '<br>PG DATATYPE NAME: ' || v_error_pg_datatype_name ||
            '<pre style="background-color:yellow; color:blue; font-size:small; line-height:normal; min-width:750px;">CONTEXT:' || replace(replace(v_error_context,'<','&lt;'),'>','&gt;') || '</pre>
            <pre style="background-color:yellow; color:blue; font-size:small; line-height:normal; min-width:750px;">SQL ERROR ' || replace(replace(sqlstate,'<','&lt;'),'>','&gt;') || ': '
            || replace(replace(sqlerrm,'<','&lt;'),'>','&gt;') || '</pre><br>' || replace(replace(array_to_string(_p_, ''),'<','&lt;'),'>','&gt;');
    else
        return '<pre style="position: fixed; top: 0; right: 0; bottom: 0; left: 0; background: rgba(0,0,0,0.8); z-index: 1000001; opacity:1;
                -webkit-transition: opacity 400ms ease-in; -moz-transition: opacity 400ms ease-in; transition: opacity 400ms ease-in;
                pointer-events: none;"></pre>
                <pre style="width: 400px; position: relative; margin: 10% auto; padding: 5px 20px 13px 20px; border-radius: 10px;
                background: #fff; background: -moz-linear-gradient(#fff, #999); background: -webkit-linear-gradient(#fff, #999);
                background: -o-linear-gradient(#fff, #999); color:black; z-index: 1000002;"><h2>An unexpected error has occurred</h2>
<p>Unfortunately, an error has occurred. This error has been recorded and will be sent to Jesla Labs. It is not known whether this error will occur repeatedly.
If this is a serious and repeatable problem, please arrange for Jesla Labs or your partner to be aware of the impact of this issue to your business.
You will need to refresh the page (using the Browser refresh button) to resume normal operation of Purgora.</p></pre>';
--    end if;
end;
$$
language plpgsql;

/* testing the function with user ID == 1
\t
select length(psp_%s('p_id=1&p_mode=edit', 1)) as f;
\q
*/
