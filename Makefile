# Makefile to compile appGoo

SHELL=/bin/bash
APXS=apxs
PGCONFIG:=$(shell find / -name pg_config -print -quit 2>/dev/null )
XXD=xxd
AGC=./agc

CREDF=pg-credentials.txt
CREDS=$(shell cat $(CREDF))
WD=$(shell pwd)
SRCS=$(shell find . -type f -name "*.ag" ! -name "*.include*" ! -path "*deleted*")
APPGOOS=$(patsubst %.ag,%.pgsql,$(SRCS))
INCDIRS=$(shell find . -type d ! -regex ".*/\..*" ! -path "*/apache2*" ! -path "*/uploaded*" ! -path "*/sql*" ! -path "*/assets*" ! -path "*/src*" )

PGFLAGS=-q -w "$(CREDS)"
APXS_CFLAGS=-I$(shell $(PGCONFIG) --includedir 2>/dev/null)
APXS_LFLAGS=-L$(shell $(PGCONFIG) --libdir 2>/dev/null)
#APXS_LIBS=$(shell $(PGCONFIG) --libs 2>/dev/null)
APXS_LIBS=-lpq -lrhash

VERSION:=1.0

.PHONY: functions install install-local-appgoo-net clean dist-clean

all: agc mod_ag.la appgoo

install: functions mod_ag.la mod_session_ag.la apache2/ag-host.inc apache2/ag-location.inc apache2/ag-sessions.inc
	@sudo ln -fst /etc/apache2/sites-available/ $(WD)/apache2/ag-pool.inc
	@sudo ln -fst /etc/apache2/sites-available/ $(WD)/apache2/ag-host.inc
	@sudo ln -fst /etc/apache2/sites-available/ $(WD)/apache2/ag-location.inc
	@sudo ln -fst /etc/apache2/sites-available/ $(WD)/apache2/ag-sessions.inc
	@sudo a2dismod session_crypto
	@sudo a2enmod cgi rewrite headers session session_cookie request expires include
	@sudo $(APXS) -i -a -n ag mod_ag.la
	@sudo $(APXS) -i -a -n session_ag mod_session_ag.la
	@sudo service apache2 restart
	@echo Apache2 server has been restarted

install-local-appgoo-net: apache2/local.appgoo.conf
	@sudo ln -fst /etc/apache2/sites-available/ $(WD)/apache2/local.appgoo.conf
	@sudo a2ensite local.appgoo
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

clean:
	@rm -f agc *~ core
	@find . -name "*~" -type f -delete
	@find . -name core -type f -delete

dist-clean: clean
	@rm -f *.pgsql make.files make.dep
	@find . -name "*.pgsql" -type f -delete

functions: agc $(CREDF) sql/ag_parse_get.include.sql sql/db_error_register.include.sql sql/error_register.include.sql sql/db_urldecode.include.sql sql/db_urlencode.include.sql
	@psql $(PGFLAGS) -f sql/ag_parse_get.include.sql
	@psql $(PGFLAGS) -f sql/error_register.include.sql
	@psql $(PGFLAGS) -f sql/db_error_register.include.sql
	@psql $(PGFLAGS) -f sql/db_urldecode.include.sql
	@psql $(PGFLAGS) -f sql/db_urlencode.include.sql

appgoo: $(CREDF) functions make.dep $(APPGOOS)

%.pgsql: %.ag $(DEPS) $(AGC)
	@($(AGC) $< > $@ && psql $(PGFLAGS) -f $@ > /dev/null) || rm -f $@

make.files: $(SRCS) src/mkfiles.pl
	@perl src/mkfiles.pl

make.dep: make.files src/mkdep.pl
	@perl src/mkdep.pl $(INCDIRS)

Makefile: make.dep
