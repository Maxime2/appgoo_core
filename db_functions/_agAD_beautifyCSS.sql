CREATE OR REPLACE FUNCTION _agAD_beautifyCSS(
    p_CSS text
    )
  RETURNS text AS
$BODY$
declare

    min_CSS text := trim(p_CSS);
    new_CSS text := '';
    i integer := 0;
    c integer := -1;
    t text := '';
    t_prev text := '';
    SQL text := $j$select coalesce(min(x.p),0) 
                    from (
                        (select position('/*' in $j$    || '$q$' || min_CSS || '$q$) p) union all '  ||
                        $k$(select position('{' in $k$  || '$r$' || min_CSS || '$r$) p)) as x
                    where x.p > 0';
    vTabs integer := 0;
    q integer := 0;

    
begin

    loop 
        
        execute SQL into c;

        if c < 1 then 
            new_CSS := new_CSS || min_CSS;
            exit;
        end if;

        t := substring(min_CSS from c for 1);
        case t
            when '/' then -- '/*'
                q := position('*/' in min_CSS) + 1;
                new_CSS := new_CSS || substring(min_CSS from 1 for q) || repeat(chr(13), 2);
                min_CSS := substring(min_CSS from q+1);
                SQL := $j$select coalesce(min(x.p),0) 
                    from (
                        (select position('/*' in $j$    || '$q$' || min_CSS || '$q$) p) union all '  ||
                        $k$(select position('{' in $k$  || '$r$' || min_CSS || '$r$) p)) as x
                    where x.p > 0';

            when '{' then
                q := c-1;
                vTabs := vTabs + 1;
                if t_prev = '{' then 
            new_CSS := new_CSS || chr(13) || repeat(chr(9), greatest(0, vTabs-1)) || substring(min_CSS from 1 for q) || ' { ';
        else
            new_CSS := new_CSS || substring(min_CSS from 1 for q) || ' { ';
        end if;
                min_CSS := trim(substring(min_CSS from c+1));
                SQL :=  $j$select coalesce(min(x.p),0) 
                    from (
                        (select position('/*' in $j$        || '$q$' || min_CSS || '$q$) p) union all '  ||
                        $k$ (select position('{'  in $k$    || '$r$' || min_CSS || '$r$) p) union all '  ||
                        $m$ (select position('}'  in $m$    || '$s$' || min_CSS || '$s$) p) union all '  ||
                        $t$ (select position(';'  in $t$    || '$t$' || min_CSS || '$t$) p)) as x
                    where x.p > 0';

            when '}' then
                if c > 1 and right(trim(substring(min_CSS from 1 for c-1)),1) != ';' then
                    min_CSS := trim(substring(min_CSS from 1 for c-1)) || ';' || substring(min_CSS from c);
                    SQL := $j$select coalesce(min(x.p),0) 
                            from (
                                (select position(';'  in $j$    || '$t$' || min_CSS || '$t$) p)) as x
                            where x.p > 0';

        else
                  q := c-1;
                  vTabs := vTabs-1;
                  new_CSS := new_CSS || chr(13) || repeat(chr(9), greatest(0, vTabs)) || '} ' || chr(13);
                  if vTabs = 0 then 
            new_CSS := new_CSS || chr(13); 
          else
                new_CSS := new_CSS || repeat(chr(9), vTabs); 
          end if;
                  min_CSS := substring(min_CSS from c+1);
                  SQL := $j$select coalesce(min(x.p),0) 
                    from (
                        (select position('/*' in $j$        || '$q$' || min_CSS || '$q$) p) union all '  ||
                        $k$ (select position('{'  in $k$    || '$r$' || min_CSS || '$r$) p) union all '  ||
                        $m$ (select position('}'  in $m$    || '$s$' || min_CSS || '$s$) p) union all '  ||
                        $t$ (select position(';'  in $t$    || '$t$' || min_CSS || '$t$) p)) as x
                    where x.p > 0';

               end if;

            when ';' then
        new_CSS := new_CSS || chr(13) || repeat(chr(9), vTabs) || substring(min_CSS from 1 for c);
        min_CSS := substring(min_CSS from c+1);
                SQL := $j$select coalesce(min(x.p),0) 
                    from (
                        (select position('/*' in $j$        || '$q$' || min_CSS || '$q$) p) union all '  ||
                        $k$ (select position('{'  in $k$    || '$r$' || min_CSS || '$r$) p) union all '  ||
                        $m$ (select position('}'  in $m$    || '$s$' || min_CSS || '$s$) p) union all '  ||
                        $t$ (select position(';'  in $t$    || '$t$' || min_CSS || '$t$) p)) as x
                    where x.p > 0';
        end case;

        t_prev := t;

    end loop;
        
    new_CSS := replace(new_CSS, ',',   ', ');
    new_CSS := replace(new_CSS, ',  ', ', ');
    new_CSS := replace(new_CSS, '>',   ' > ');
    new_CSS := replace(new_CSS, ' >  ', ' > ');
    new_CSS := replace(new_CSS, '  >  ', ' > ');
    -- replace the additional linebreak that is added to nested {}
    new_CSS := replace(new_CSS, '}

    }', '}
    }');    

    return new_CSS;


EXCEPTION
    WHEN OTHERS THEN
        return p_CSS;
end;
$BODY$
  LANGUAGE plpgsql VOLATILE
  COST 100;
