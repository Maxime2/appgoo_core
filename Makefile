# Makefile to compile appGoo

SHELL=/bin/bash
APXS=apxs
PGCONFIG:=$(shell find / -name pg_config -print -quit 2>/dev/null )
XXD=xxd

CREDF=pg-credentials.txt
CREDS=$(shell cat $(CREDF))
PGFLAGS=-q -w "$(CREDS)"
APXS_CFLAGS=-I$(shell $(PGCONFIG) --includedir 2>/dev/null)
APXS_LFLAGS=-L$(shell $(PGCONFIG) --libdir 2>/dev/null)
#APXS_LIBS=$(shell $(PGCONFIG) --libs 2>/dev/null)
APXS_LIBS=-lpq -lrhash

VERSION:=1.0

all: agc mod_ag.la

install:
	@sudo $(APXS) -i -a -n ag mod_ag.la
	@sudo $(APXS) -i -a -n session_ag mod_session_ag.la
	@sudo service apache2 restart
	@echo Apache2 server has been restarted


agc: src/agc.c src/template_pgsql_function_begin_sql.h src/template_pgsql_function_end_sql.h sql/db_drop_function.include.sql
	@$(CC) $(CFLAGS) -o $@ $<
	@psql $(PGFLAGS) -f sql/db_drop_function.include.sql


mod_ag.la: src/mod_ag.c
	@$(APXS) -c -o $@ $(APXS_CFLAGS) $(APXS_LFLAGS) $(APXS_LIBS) $< --shared

mod_session_ag.la: src/mod_session_ag.c
	@$(APXS) -c -o $@ $(APXS_CFLAGS) $(APXS_LFLAGS) $(APXS_LIBS) $< --shared

src/template_pgsql_function_begin_sql.h: src/template_pgsql_function_begin.sql
	@$(XXD) -i $< $@

src/template_pgsql_function_end_sql.h: src/template_pgsql_function_end.sql
	@$(XXD) -i $< $@
