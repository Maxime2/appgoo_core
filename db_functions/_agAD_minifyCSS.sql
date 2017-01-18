CREATE OR REPLACE FUNCTION _agAD_minifyCSS(
    p_CSS text
    )
  RETURNS text AS
$BODY$
declare

    /* Note that we keep comments that are earmarked with an ! after the slash-*   */

    old_CSS text := trim(replace(p_CSS, '/*!', '&#47;&#42;&#33;'));
    new_CSS text := '';
    i integer := 0;
    j integer := 0;
    
    
begin

    --remove comments, but we keep comments marked as important that have an !
    loop
        i := position('/*' in old_CSS);
        if i = 0 then exit; end if;
        j := position('*/' in substring(old_CSS from i));
        if j > 1 then 
            if i > 1 then 
                old_CSS := substring(old_CSS from 1 for i-1) || substring(old_CSS from i+j+2);
            else
                old_CSS := substring(old_CSS from j+2);
            end if;
        else 
            -- there is a problem
            return '/*! appGoo function LOOP error */' || chr(13) || p_CSS;
        end if;
    end loop;

    new_CSS := replace(old_CSS, '&#47;&#42;&#33;', '/*!');
    new_CSS := replace(new_CSS, chr(10), '');
    new_CSS := replace(new_CSS, chr(11), '');
    new_CSS := replace(new_CSS, chr(12), '');
    new_CSS := replace(new_CSS, chr(13), '');
    new_CSS := replace(new_CSS, chr(9),  '');
    new_CSS := replace(new_CSS, '  ', ' ');
    new_CSS := replace(new_CSS, '  ', ' ');
    new_CSS := replace(new_CSS, '  ', ' ');
    new_CSS := replace(new_CSS, ', ', ',');
    new_CSS := replace(new_CSS, ': ', ':');
    new_CSS := replace(new_CSS, ' > ', '>');
    new_CSS := replace(new_CSS, '> ', '>');
    new_CSS := replace(new_CSS, ' >', '>');
    new_CSS := replace(new_CSS, '/*', chr(13) || '/*');
    new_CSS := replace(new_CSS, '*/', '*/' || chr(13));
    new_CSS := replace(new_CSS, ' {', '{');
    new_CSS := replace(new_CSS, '{ ', '{');
    new_CSS := replace(new_CSS, ' }', '}');
    new_CSS := replace(new_CSS, '} ', '}');
    new_CSS := replace(new_CSS, '; ', ';');
    
    return new_CSS;

EXCEPTION
    WHEN OTHERS THEN
        return '/*! appGoo function error */' || chr(13) || p_CSS;
end;
$BODY$
  LANGUAGE plpgsql VOLATILE
  COST 100;
