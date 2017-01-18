CREATE OR REPLACE FUNCTION _agUM_securityCheck(
    p_userID bigint, 
    p_dataPartitionFilterID bigint,
    p_calledFromObjectID bigint,
    p_arraySARS bigint[]
    )
  RETURNS BOOLEAN AS
$BODY$
declare

    vSARS bigint[];
    r record;   
    vBool  boolean := false;
    vSQL text := '';
    
begin

    vSARS :=  select array(
                select distinct(case sr.isDynamic when true then id else (case coalesce(sr.grantAccess, false) when false then -1 else 0 end) end) 
                from agUM_securityRights sr join _agUM_SecurityRoleRights rr on sr.id = rr.securityRightID
                join _agUM_UserSecurityRoles ur on rr.roleID = ur.roleID 
                where sr.securityCode = ANY(p_arraySARS)
                and ur.userID = p_userID)
            into vSARS;


    if 0 = ANY(vSARS) then 
        return true;
    else
        for r in (
            select dynamicCode
            from agUM_securityRights
            where id = ANY(vSARS)  
        ) loop 
            vSQL := regexp_replace(regexp_replace(r.dynamicCode, 'p_calledFromObjectID', p_calledFromObjectID, 'gi'), 'p_dataPartitionFilterID', p_dataPartitionFilterID, 'gi');
            vSQL := regexp_replace(vSQL, 'p_userID', p_userID, 'gi');
            execute vSQL into vBool;
            if vBool then return true; end if;
        end loop;
        -- because nothing returned true (or all results were false and therefore record returned no results, return false)
        return false;
    end if;

    --it should never get this far
    return false;

EXCEPTION
    WHEN OTHERS THEN
        return false;
end;
$BODY$
  LANGUAGE plpgsql VOLATILE
  COST 100;

CREATE OR REPLACE FUNCTION _agUM_securityCheck(
    p_userID bigint, 
    p_dataPartitionFilterID bigint,
    p_calledFromObjectID bigint,
    p_SAR bigint
    )
  RETURNS BOOLEAN AS
$BODY$
declare

    
begin

    return _agUM_securityCheck(p_userID, p_dataPartitionFilterID, p_calledFromObjectID, ARRAY[p_SAR]::bigint[]);

EXCEPTION
    WHEN OTHERS THEN
        return false;
end;
$BODY$
  LANGUAGE plpgsql VOLATILE
  COST 100;