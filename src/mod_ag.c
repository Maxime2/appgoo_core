/*
 * mod_ag.c
 *
*/
/* PostgreSQL pool
   Originally based on mod_dbi_pool.c by Paul Querna, 
   http://www.outoforder.cc/projects/apache/mod_dbi_pool/
 */
/* Upload filter
   Originally based on mod_upload by Nick Kew,
   http://apache.webthing.com/mod_upload/
*/
/* Authentication 
   Originally based on mod_auth_form
*/

//#define DEBUG 0

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <libpq-fe.h>
#include <rhash.h>   /* in librhash-dev package */
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/utsname.h>

#include "httpd.h"
#include "http_core.h"
#include "http_config.h"
#include "http_log.h"
#include "http_protocol.h"
#include "http_request.h"
#include "apr.h"
#include "apr_reslist.h"
#include "apr_strings.h"
#include "apr_escape.h"
#include "apr_lib.h"                /* for apr_isspace */
#include "apr_base64.h"             /* for apr_base64_decode et al */


/* for apr_filepath_list_split_impl, as defined in include/arch/apr_private_common.h, see APR sources */
#include "apr_pools.h"
#include "apr_tables.h"

apr_status_t apr_filepath_list_split_impl(apr_array_header_t **pathelts,
                                          const char *liststr,
                                          char separator,
                                          apr_pool_t *p);
/* */


#define APR_WANT_STRFUNC            /* for strcasecmp */
#include "apr_want.h"
#include "util_script.h"

#include "mod_auth.h"
#include "mod_session.h"
#include "mod_request.h"
#include "util_md5.h"
#include "ap_expr.h"

#define spit_pg_error(st) { ap_rprintf(r,"<!-- "); ap_rprintf(r,"Cannot %s: %s\n",st,PQerrorMessage(pgc)); ap_rprintf(r," -->\n"); }
#define spit_pg_error_syslog(st) ap_log_error(APLOG_MARK, APLOG_CRIT, 0, r->server, "Cannot %s: %s\n", st,PQerrorMessage(pgc));
#define MAX_ALLOWED_PAGES 100

#define true 1
#define false 0
#define BUFLEN 8192
#define KEEPONCLOSE APR_CREATE | APR_READ | APR_WRITE | APR_EXCL
#define UPLOADED_PERMS APR_UREAD | APR_UWRITE | APR_GREAD | APR_WREAD
//#define KEEPONCLOSE APR_CREATE | APR_READ | APR_WRITE | APR_EXCL | APR_DELONCLOSE
/* use __(xx) macro for debug logging */
#define __(s, ...)  ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s, __VA_ARGS__) ;

#define ISINT(val)						\
  for ( p = val; *p; ++p)					\
    if ( ! isdigit(*p) )					\
      return "Argument must be numeric!"

#define clean_up_connection(s)			\
  PQclear(pgr),					\
    ag_pool_close(s, pgc),			\
    OK;

#define FORM_LOGIN_HANDLER "purgora-login-handler"
#define FORM_LOGOUT_HANDLER "purgora-logout-handler"
#define FORM_REDIRECT_HANDLER "purgora-redirect-handler"
#define MOD_AUTH_FORM_HASH "site"
#define AUTHN_PURGORA_PROVIDER "purgora"

PGconn* ag_pool_open(server_rec* s);
void ag_pool_close(server_rec* s, PGconn* sql);

extern module AP_MODULE_DECLARE_DATA ag_module ;

typedef struct ag_config {
  const char * allowed[MAX_ALLOWED_PAGES];
  const char * connection_string;
  int connection_string_set;
  const char * key;
  int key_set;
  const char * ServerName;
  int ServerName_set;
  apr_pool_t * pool;
  apr_reslist_t* dbpool ;
  int nmin, nmin_set ;
  int nkeep, nkeep_set ;
  int nmax, nmax_set ;
  int exptime, exptime_set ;
  int is_enabled, is_enabled_set;
  int allowed_count, allowed_count_set;
  int environment, environment_set;
  int mode, mode_set;
} ag_cfg;

typedef struct {
  char* file_field ;
  int file_field_set;
  char* folder_field ;
  int folder_field_set;
  char* dir_field ;
  int dir_field_set;
  apr_size_t form_size ;
  int form_size_set;
    char *dir;
    int authoritative;
    int authoritative_set;
    const char *site;
    int site_set;
    const char *username;
    int username_set;
    const char *password;
    int password_set;
    apr_size_t max_form_size ;
    int max_form_size_set;
    int fakebasicauth;
    int fakebasicauth_set;
    const char *location;
    int location_set;
    const char *method;
    int method_set;
    const char *mimetype;
    int mimetype_set;
    const char *body;
    int body_set;
    const char *content_type;
    int content_type_set;
    int disable_no_store;
    int disable_no_store_set;
    ap_expr_info_t *loginsuccess;
    int loginsuccess_set;
    ap_expr_info_t *loginrequired;
    int loginrequired_set;
    ap_expr_info_t *logout;
    int logout_set;
} ag_dir_cfg ;

typedef struct upload_ctx {
  apr_pool_t* pool ;
  server_rec *server;
  apr_table_t* form ;
  char* boundary ;
  char* key ;
  char* val ;
  char* file_field ;
  char* leftover ;
  apr_size_t leftsize;
  enum { p_none, p_head, p_field, p_end, p_done } parse_state ;
  char is_file ;
} upload_ctx ;

typedef struct {
  request_rec *r;
  char *args;
} params_t;


typedef enum { cmd_setkey, cmd_connection, cmd_allowed, cmd_enabled,
	       cmd_min, cmd_keep, cmd_max, cmd_exp } cmd_parts ;

static apr_hash_t *ag_pool_config;

static apr_hash_t *ag_pool_config;
static int (*ap_session_load_fn) (request_rec * r, session_rec ** z) = NULL;
static apr_status_t (*ap_session_get_fn)(request_rec * r, session_rec * z,
        const char *key, const char **value) = NULL;
static apr_status_t (*ap_session_set_fn)(request_rec * r, session_rec * z,
        const char *key, const char *value) = NULL;
static void (*ap_request_insert_filter_fn) (request_rec * r) = NULL;
static void (*ap_request_remove_filter_fn) (request_rec * r) = NULL;


static APR_DECLARE_NONSTD(void *) apr_pmemcat(apr_pool_t *p, apr_size_t *len, ... ) {
  va_list args;
  void *m, *res, *cm;

  *len = 0;

  va_start(args, len);
  while ((m = va_arg(args, void *)) != NULL) {
    apr_size_t memlen = va_arg(args, apr_size_t);
    *len += memlen;
  }
  va_end(args);

  cm = res = (void *) apr_palloc(p, *len);

  va_start(args, len);
  while ((m = va_arg(args, void *)) != NULL) {
    apr_size_t memlen = va_arg(args, apr_size_t);
    if (memlen == 0) continue;
    memcpy(cm, m, memlen);
    cm += memlen;
  }
  va_end(args);

  return res;
}



static void* create_ag_config(apr_pool_t* p, server_rec* s) {
  ag_cfg* config = (ag_cfg*) apr_pcalloc(p, sizeof(ag_cfg)) ;
  config->is_enabled = true;
  config->connection_string = NULL;
  config->allowed_count = 0;
  config->nmax = 1;
  config->exptime = 3600000;
  config->pool = p;
  config->environment = 1;
  config->mode = 99;
  return config ;
}

static void *merge_ag_config(apr_pool_t * p, void *basev, void *addv) {
    ag_cfg *new = (ag_cfg *) apr_pcalloc(p, sizeof(ag_cfg));
    ag_cfg *add = (ag_cfg *) addv;
    ag_cfg *base = (ag_cfg *) basev;

    new->key = (add->key_set == 0) ? base->key : add->key;
    new->key_set = add->key_set || base->key_set;
    new->connection_string = (add->connection_string_set == 0) ? base->connection_string : add->connection_string;
    new->connection_string_set = add->connection_string_set || base->connection_string_set;
    new->ServerName = (add->ServerName_set == 0) ? base->ServerName : add->ServerName;
    new->ServerName_set = add->ServerName_set || base->ServerName_set;
    /* NB: ag_cfg->pool and ag_cfg->dbpool must not be merged */
    new->pool = add->pool;
    new->dbpool = add->dbpool;
    new->nmin = (add->nmin_set == 0) ? base->nmin : add->nmin;
    new->nmin_set = add->nmin_set || base->nmin_set;
    new->nkeep = (add->nkeep_set == 0) ? base->nkeep : add->nkeep;
    new->nkeep_set = add->nkeep_set || base->nkeep_set;
    new->nmax = (add->nmax_set == 0) ? base->nmax : add->nmax;
    new->nmax_set = add->nmax_set || base->nmax_set;
    new->exptime = (add->exptime_set == 0) ? base->exptime : add->exptime;
    new->exptime_set = add->exptime_set || base->exptime_set;
    new->is_enabled = (add->is_enabled_set == 0) ? base->is_enabled : add->is_enabled;
    new->is_enabled_set = add->is_enabled_set || base->is_enabled_set;
    new->environment = (add->environment_set == 0) ? base->environment : add->environment;
    new->environment_set = add->environment_set || base->environment_set;
    new->mode = (add->mode_set == 0) ? base->mode : add->mode;
    new->mode_set = add->mode_set || base->mode_set;
    

    return new;
}


static void* create_dir_config(apr_pool_t* p, char* x) {
  ag_dir_cfg *conf = apr_pcalloc(p, sizeof(ag_dir_cfg)) ;

  conf->form_size = 8 ;

  conf->dir = x;
  /* Any failures are fatal. */
  conf->authoritative = 1;

  /* form size defaults to 8k */
  conf->max_form_size = HUGE_STRING_LEN;

  /* default form field names */
  conf->username = "p_login";
  conf->password = "p_password";
  conf->location = "p_location";
  conf->method = "p_method";
  conf->mimetype = "p_mimetype";
  conf->body = "p_body";
  conf->file_field = "p_file";
  conf->folder_field = "p_folder";
  conf->dir_field = NULL;
  
  return conf ;
}

static void *merge_dir_config(apr_pool_t * p, void *basev, void *addv) {
    ag_dir_cfg *new = (ag_dir_cfg *) apr_pcalloc(p, sizeof(ag_dir_cfg));
    ag_dir_cfg *add = (ag_dir_cfg *) addv;
    ag_dir_cfg *base = (ag_dir_cfg *) basev;

    new->file_field = (add->file_field_set == 0) ? base->file_field : add->file_field;
    new->file_field_set = add->file_field_set || base->file_field_set;
    new->folder_field = (add->folder_field_set == 0) ? base->folder_field : add->folder_field;
    new->folder_field_set = add->folder_field_set || base->folder_field_set;
    new->dir_field = (add->dir_field_set == 0) ? base->dir_field : add->dir_field;
    new->dir_field_set = add->dir_field_set || base->dir_field_set;
    new->authoritative = (add->authoritative_set == 0) ? base->authoritative : add->authoritative;
    new->authoritative_set = add->authoritative_set || base->authoritative_set;
    new->site = (add->site_set == 0) ? base->site : add->site;
    new->site_set = add->site_set || base->site_set;
    new->username = (add->username_set == 0) ? base->username : add->username;
    new->username_set = add->username_set || base->username_set;
    new->password = (add->password_set == 0) ? base->password : add->password;
    new->password_set = add->password_set || base->password_set;
    new->location = (add->location_set == 0) ? base->location : add->location;
    new->location_set = add->location_set || base->location_set;
    new->form_size = (add->form_size_set == 0) ? base->form_size : add->form_size;
    new->form_size_set = add->form_size_set || base->form_size_set;
    new->max_form_size = (add->max_form_size_set == 0) ? base->max_form_size : add->max_form_size;
    new->max_form_size_set = add->max_form_size_set || base->max_form_size_set;
    new->fakebasicauth = (add->fakebasicauth_set == 0) ? base->fakebasicauth : add->fakebasicauth;
    new->fakebasicauth_set = add->fakebasicauth_set || base->fakebasicauth_set;
    new->method = (add->method_set == 0) ? base->method : add->method;
    new->method_set = add->method_set || base->method_set;
    new->mimetype = (add->mimetype_set == 0) ? base->mimetype : add->mimetype;
    new->mimetype_set = add->mimetype_set || base->mimetype_set;
    new->body = (add->body_set == 0) ? base->body : add->body;
    new->body_set = add->body_set || base->body_set;
    new->disable_no_store = (add->disable_no_store_set == 0) ? base->disable_no_store : add->disable_no_store;
    new->disable_no_store_set = add->disable_no_store_set || base->disable_no_store_set;
    new->loginsuccess = (add->loginsuccess_set == 0) ? base->loginsuccess : add->loginsuccess;
    new->loginsuccess_set = add->loginsuccess_set || base->loginsuccess_set;
    new->loginrequired = (add->loginrequired_set == 0) ? base->loginrequired : add->loginrequired;
    new->loginrequired_set = add->loginrequired_set || base->loginrequired_set;
    new->logout = (add->logout_set == 0) ? base->logout : add->logout;
    new->logout_set = add->logout_set || base->logout_set;
    new->content_type = (add->content_type_set == 0) ? base->content_type : add->content_type;
    new->content_type_set = add->content_type_set || base->content_type_set;

    return new;
}

static const char* set_filename(cmd_parms* cmd, void* cfg, const char* name) {
  ag_dir_cfg *conf = (ag_dir_cfg*) cfg ;
  conf->file_field = apr_pstrdup(cmd->pool, name) ;
  conf->file_field_set = 1;
  return NULL ;
}

static const char* set_foldername(cmd_parms* cmd, void* cfg, const char* name) {
  ag_dir_cfg *conf = (ag_dir_cfg*) cfg ;
  conf->folder_field = apr_pstrdup(cmd->pool, name) ;
  conf->folder_field_set = 1;
  return NULL ;
}

static const char* set_directory(cmd_parms* cmd, void* cfg, const char* name) {
  ag_dir_cfg *conf = (ag_dir_cfg*) cfg ;
  conf->dir_field = apr_pstrdup(cmd->pool, name) ;
  conf->dir_field_set = 1;
  return NULL ;
}

static const char* set_formsize(cmd_parms* cmd, void* cfg, const char* sz) {
  ag_dir_cfg *conf = (ag_dir_cfg*) cfg ;
  apr_off_t size;
  if (APR_SUCCESS != apr_strtoff(&size, sz, NULL, 10)
      || size < 0 || size > APR_SIZE_MAX) {
    return "agUploadFormSize must be a size in bytes, or zero.";
  }
  conf->form_size = (apr_size_t)size ;
  conf->form_size_set = 1;
  return NULL ;
}

static const char *check_string(cmd_parms * cmd, const char *string) {
  if (!string || !*string || ap_strchr_c(string, '=') || ap_strchr_c(string, '&')) {
    return apr_pstrcat(cmd->pool, cmd->directive->directive,
		       " cannot be empty, or contain '=' or '&'.",
		       NULL);
  }
  return NULL;
}

static const char *set_cookie_form_username(cmd_parms * cmd, void *config, const char *username) {
  ag_dir_cfg *conf = (ag_dir_cfg *) config;
  conf->username = username;
  conf->username_set = 1;
  return check_string(cmd, username);
}

static const char *set_cookie_form_password(cmd_parms * cmd, void *config, const char *password) {
    ag_dir_cfg *conf = (ag_dir_cfg *) config;
    conf->password = password;
    conf->password_set = 1;
    return check_string(cmd, password);
}

static const char *set_cookie_form_location(cmd_parms * cmd, void *config, const char *location) {
    ag_dir_cfg *conf = (ag_dir_cfg *) config;
    conf->location = location;
    conf->location_set = 1;
    return check_string(cmd, location);
}

static const char *set_cookie_form_method(cmd_parms * cmd, void *config, const char *method) {
    ag_dir_cfg *conf = (ag_dir_cfg *) config;
    conf->method = method;
    conf->method_set = 1;
    return check_string(cmd, method);
}

static const char *set_cookie_form_mimetype(cmd_parms * cmd, void *config, const char *mimetype) {
    ag_dir_cfg *conf = (ag_dir_cfg *) config;
    conf->mimetype = mimetype;
    conf->mimetype_set = 1;
    return check_string(cmd, mimetype);
}

static const char *set_cookie_form_body(cmd_parms * cmd, void *config, const char *body) {
    ag_dir_cfg *conf = (ag_dir_cfg *) config;
    conf->body = body;
    conf->body_set = 1;
    return check_string(cmd, body);
}

static const char *set_cookie_form_size(cmd_parms * cmd, void *config, const char *arg) {
  ag_dir_cfg *conf = (ag_dir_cfg *)config;
  apr_off_t size;

  if (APR_SUCCESS != apr_strtoff(&size, arg, NULL, 10)
      || size < 0 || size > APR_SIZE_MAX) {
    return "AuthFormSize must be a size in bytes, or zero.";
  }
  conf->max_form_size = (apr_size_t)size;
  conf->max_form_size_set = 1;

  return NULL;
}

static const char *set_login_required_location(cmd_parms * cmd, void *config, const char *loginrequired) {
    ag_dir_cfg *conf = (ag_dir_cfg *) config;
    const char *err;

    conf->loginrequired = ap_expr_parse_cmd(cmd, loginrequired, AP_EXPR_FLAG_STRING_RESULT,
                                        &err, NULL);
    if (err) {
        return apr_psprintf(cmd->pool,
                            "Could not parse login required expression '%s': %s",
                            loginrequired, err);
    }
    conf->loginrequired_set = 1;

    return NULL;
}

static const char *set_login_success_location(cmd_parms * cmd, void *config, const char *loginsuccess) {
    ag_dir_cfg *conf = (ag_dir_cfg *) config;
    const char *err;

    conf->loginsuccess = ap_expr_parse_cmd(cmd, loginsuccess, AP_EXPR_FLAG_STRING_RESULT,
                                        &err, NULL);
    if (err) {
        return apr_psprintf(cmd->pool,
                            "Could not parse login success expression '%s': %s",
                            loginsuccess, err);
    }
    conf->loginsuccess_set = 1;

    return NULL;
}

static const char *set_logout_location(cmd_parms * cmd, void *config, const char *logout) {
    ag_dir_cfg *conf = (ag_dir_cfg *) config;
    const char *err;

    conf->logout = ap_expr_parse_cmd(cmd, logout, AP_EXPR_FLAG_STRING_RESULT,
                                        &err, NULL);
    if (err) {
        return apr_psprintf(cmd->pool,
                            "Could not parse logout required expression '%s': %s",
                            logout, err);
    }
    conf->logout_set = 1;

    return NULL;
}

static const char *set_site_passphrase(cmd_parms * cmd, void *config, const char *site) {
    ag_dir_cfg *conf = (ag_dir_cfg *) config;
    conf->site = site;
    conf->site_set = 1;
    return NULL;
}

static const char *set_authoritative(cmd_parms * cmd, void *config, int flag) {
    ag_dir_cfg *conf = (ag_dir_cfg *) config;
    conf->authoritative = flag;
    conf->authoritative_set = 1;
    return NULL;
}

static const char *set_fake_basic_auth(cmd_parms * cmd, void *config, int flag) {
  ag_dir_cfg *conf = (ag_dir_cfg *) config;
  conf->fakebasicauth = flag;
  conf->fakebasicauth_set = 1;
  return NULL;
}

static const char *set_disable_no_store(cmd_parms * cmd, void *config, int flag) {
    ag_dir_cfg *conf = (ag_dir_cfg *) config;
    conf->disable_no_store = flag;
    conf->disable_no_store_set = 1;
    return NULL;
}

static const char *set_content_type(cmd_parms * cmd, void *config, const char *content_type) {
  ag_dir_cfg *conf = (ag_dir_cfg *) config;
  conf->content_type = content_type;
  conf->content_type_set = 1;
  return NULL;
}

static const char* set_param(cmd_parms* cmd, void* cfg,
	const char* val) {
  const char* p ;
  ag_cfg* ag = (ag_cfg*) ap_get_module_config(cmd->server->module_config, &ag_module ) ;

  switch ( (intptr_t) cmd->info ) {
  case cmd_setkey:
    /*
    if (strlen(conn_id) > 255) {
        return APR_EGENERAL;
    }
    for (c = 0; c < strlen(conn_id); c++) {
        if (conn_id[c] < ' ') {
            return APR_EGENERAL;
        }
    }
    */
    ag->key = val;
    apr_hash_set(ag_pool_config, ag->key, APR_HASH_KEY_STRING, ag);
    ag->key_set = 1;
    break ;
  case cmd_connection: ag->connection_string = val ;
    ag->connection_string_set = 1;
    break ;
  case cmd_allowed:
    if (ag->allowed_count < MAX_ALLOWED_PAGES)
      ag->allowed[ag->allowed_count++] = val;
    break;
  case cmd_min: ISINT(val) ; ag->nmin = atoi(val) ;
    ag->nmin_set = 1;
    break ;
  case cmd_keep: ISINT(val) ; ag->nkeep = atoi(val) ;
    ag->nkeep_set = 1;
    break ;
  case cmd_max: ISINT(val) ; ag->nmax = atoi(val) ;
    ag->nmax_set = 1;
    break ;
  case cmd_exp: ISINT(val) ; ag->exptime = atoi(val) ;
    ag->exptime_set = 1;
    break ;
  case cmd_enabled:
    if (!strcasecmp(val, "on")) ag->is_enabled = true;
    else ag->is_enabled = false;
    ag->is_enabled_set = 1;
    break;
  }
  return NULL ;
}


/************ ag cfg: manage db connection pool ****************/
/* an apr_reslist_constructor for PgSQL connections */

static apr_status_t ag_pool_construct(void** db, void* params, apr_pool_t* pool) {
  ag_cfg* ag = (ag_cfg*) params ;
  PGconn* sql = PQconnectdb (ag->connection_string);
  *db = sql ;

  if ( sql )
    return APR_SUCCESS ;
  else
    return APR_EGENERAL ;
}

static apr_status_t ag_pool_destruct(void* sql, void* params, apr_pool_t* pool) {
  PQfinish((PGconn*)sql) ;
  return APR_SUCCESS ;
}

static int setup_db_pool(apr_pool_t* p, apr_pool_t* plog,
	apr_pool_t* ptemp, server_rec* s) {

  void *data = NULL;
  char *key;
  const char *userdata_key = "ag_post_config";
  apr_hash_index_t *idx;
  apr_ssize_t len;
  ag_cfg *ag;

  // This code is used to prevent double initialization of the module during Apache startup
  apr_pool_userdata_get(&data, userdata_key, s->process->pool);
  if ( data == NULL ) { 
    apr_pool_userdata_set((const void *)1, userdata_key, apr_pool_cleanup_null, s->process->pool);
    return OK; 
  }

  for (idx = apr_hash_first(p, ag_pool_config); idx; idx = apr_hash_next(idx)) {

    apr_hash_this(idx, (void *) &key, &len, (void *) &ag);

    if ( apr_reslist_create(&ag->dbpool,
			    ag->nmin,
			    ag->nkeep,
			    ag->nmax,
			    ag->exptime,
			    ag_pool_construct,
			    ag_pool_destruct,
			    (void*)ag, p) != APR_SUCCESS ) {
      ap_log_error(APLOG_MARK, APLOG_CRIT, 0, s, "mod_ag: failed to initialise") ;
      return 500 ;
    }
    apr_pool_cleanup_register(p, ag->dbpool,
			      (void*)apr_reslist_destroy,
			      apr_pool_cleanup_null) ;
    /*
     * XXX: If no new connection could be opened, the reslist will not
     * contain any resources and reslist_create will fail, giving 
     * val->pool the value NULL and making it unusable. The configuration 
     * set is not really recoverable in this case. For now we log this condition
     * and the authentication functions protect themselves against this 
     * condition. A much better way would be to free the configuration
     * set completely. But to do this other changes are necessary, so for
     * now this is basically a workaround. This has to be fixed before
     * 1.0 
     */
    apr_hash_set(ag_pool_config, key, APR_HASH_KEY_STRING, ag);

  }
  return OK ;
}


/* Functions we export for modules to use:
	- open acquires a connection from the pool (opens one if necessary)
	- close releases it back in to the pool
*/
PGconn* ag_pool_open(server_rec* s) {
  PGconn* ret = NULL ;
  ag_cfg* ag = (ag_cfg*)
	ap_get_module_config(s->module_config, &ag_module) ;
  apr_uint32_t acquired_cnt ;

  //  __(s, " DBPOOL: %s", ag->dbpool == NULL ? "NULL" : "not NULL");
  
  if (ag->dbpool == NULL) {
    ag = apr_hash_get(ag_pool_config, ag->key, APR_HASH_KEY_STRING);
  }
  if ( apr_reslist_acquire(ag->dbpool, (void**)&ret) != APR_SUCCESS ) {
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, "mod_ag: Failed to acquire PgSQL connection from pool!") ;
    return NULL ;
  }
  if (PQstatus(ret) != CONNECTION_OK) {
    PQreset(ret);
    if (PQstatus(ret) != CONNECTION_OK) {
      ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
	"PgSQL Error: %s", PQerrorMessage(ret) ) ;
      apr_reslist_release(ag->dbpool, ret) ;
      return NULL ;
    }
  }
  if (ag->nkeep < (acquired_cnt = apr_reslist_acquired_count	( ag->dbpool	))) {
    ap_log_error(APLOG_MARK, APLOG_WARNING, 0, s, "mod_ag: %d connections in the %s pool acquired (%d,%d,%d)",
		 acquired_cnt, ag->key, ag->nmin, ag->nkeep, ag->nmax
		 ) ;
  }
  return ret ;
}

void ag_pool_close(server_rec* s, PGconn* sql) {
  ag_cfg* ag = (ag_cfg*)
	ap_get_module_config(s->module_config, &ag_module) ;
  if (ag->dbpool == NULL) {
    ag = apr_hash_get(ag_pool_config, ag->key, APR_HASH_KEY_STRING);
  }
  apr_reslist_release(ag->dbpool, sql) ;
}


/* Export the form data we've parsed */
apr_table_t* upload_form(request_rec* r) {
  return (apr_table_t*)
	ap_get_module_config(r->request_config, &ag_module);
}





//Reading the request body into memory

static int util_read(request_rec *r, const char **rbuf, apr_off_t *size)
{
    /*~~~~~~~~*/
    int rc = OK;
    /*~~~~~~~~*/

    if((rc = ap_setup_client_block(r, REQUEST_CHUNKED_ERROR))) {
        return(rc);
    }

    if(ap_should_client_block(r)) {

        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
        char         argsbuffer[HUGE_STRING_LEN];
        apr_off_t    rsize, len_read, rpos = 0;
        apr_off_t length = r->remaining;
        /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

        *rbuf = (const char *) apr_pcalloc(r->pool, (apr_size_t) (length + 1));
        *size = length;
        while((len_read = ap_get_client_block(r, argsbuffer, sizeof(argsbuffer))) > 0) {
            if((rpos + len_read) > length) {
                rsize = length - rpos;
            }
            else {
                rsize = len_read;
            }

            memcpy((char *) *rbuf + rpos, argsbuffer, (size_t) rsize);
            rpos += rsize;
        }
    }
    return(rc);
}

/* apr_table_t iteration callbacks */
static int tab_cb(void *data, const char *key, const char *value) {
    server_rec *s = data;
    __(s, "GET[%s]: %s", key, value);
    return TRUE;/* TRUE:continue iteration. FALSE:stop iteration */
}
static int tab_args(void *data, const char *key, const char *value) {
  params_t *params = (params_t*) data;
  const char *encoded_value = apr_pescape_urlencoded(params->r->pool, value);
  if (params->args) {
    params->args = apr_pstrcat(params->r->pool, params->args, "&", key, "=", apr_pescape_urlencoded(params->r->pool, value), NULL);
  } else {
    params->args = apr_pstrcat(params->r->pool, key, "=", apr_pescape_urlencoded(params->r->pool, value), NULL);
  }
  return TRUE;/* TRUE:continue iteration. FALSE:stop iteration */
}




static int ag_handler (request_rec * r)
{
   char *cursor_string;
   apr_table_t * GET = NULL, *GETargs = NULL, *NEW = NULL;
   apr_array_header_t * POST;
   ag_cfg* config = (ag_cfg*) ap_get_module_config(r->server->module_config, &ag_module ) ;
   ag_dir_cfg* upload_config = (ag_dir_cfg*) ap_get_module_config(r->per_dir_config, &ag_module ) ;
   char * requested_file;
   PGconn * pgc;
   PGresult * pgr;
   int i, j, allowed_to_serve, filename_length = 0;
   int field_count, tuple_count;
   int end = 0;
   const char * request_args = NULL;
   const char* ctype = apr_table_get(r->headers_in, "Content-Type") ;
   apr_off_t request_size;
   apr_bucket *b;
   apr_bucket_brigade *bb;
   apr_status_t status;
   char error_buf[1024];
   apr_size_t bytes, count = 0;
   const char *buf;
   char *temp_filename = NULL;
   char *basename;
   params_t params;
   apr_array_header_t *pathelts;

   /* PQexecParams doesn't seem to like zero-length strings, so we feed it a dummy */
   const char * dummy_get = "nothing";
   const char * dummy_user = "nobody";

   const char * cursor_values[258] = { r -> args ? apr_pstrdup(r->pool, r->args) : dummy_get, r->user ? r->user : dummy_user };
   int cursor_value_lengths[258] = { strlen(cursor_values[0]), strlen(cursor_values[1]) };
   int cursor_value_formats[258] = { 0 };

   //   __(r->server, " HANDLER: %s", (r->handler) ? r->handler : "<null>");
   //   __(r->server, " METHOD: %s", (r->method) ? r->method : "<null>");
   
   if (!r -> handler || strcmp (r -> handler, "ag-handler") ) return DECLINED;
   //    __(r->server, " ** AG HANDLER");
		
   if (!r -> method || (strcmp (r -> method, "GET") && strcmp (r -> method, "POST")) ) return DECLINED;

   //   __(r->server, " ENABLED: %s", (config->is_enabled == true) ? "true" : "false");
   
   //   if (config->is_enabled != true) return OK; /* pretending we have responded, may return DECLINED in the future */

   if (ctype && !strncmp ( ctype , "multipart/form-data", 19 ) ) {

     bb = apr_brigade_create(r->pool, r->connection->bucket_alloc);
     do {
       status = ap_get_brigade(r->input_filters, bb, AP_MODE_READBYTES, APR_BLOCK_READ, BUFLEN);
       if (status == APR_SUCCESS) {
	 for (b = APR_BRIGADE_FIRST(bb);
	      b != APR_BRIGADE_SENTINEL(bb);
	      b = APR_BUCKET_NEXT(b) ) {
	   if (APR_BUCKET_IS_EOS(b)) {
	     end = 1;
	     break;
	   } else if (APR_BUCKET_IS_METADATA(b)) {
	     continue;
	   } else if (APR_SUCCESS == apr_bucket_read(b, &buf, &bytes, APR_BLOCK_READ)) {
	     char *x = apr_pstrndup(r->pool, buf, bytes);
	     if (temp_filename != NULL)
	       temp_filename = apr_pstrcat(r->pool, temp_filename, x, NULL);
	     else
	       temp_filename = x;
	     count += bytes;
	   } else {
	     __(r->server, "READ tmpfilename READ BUCKET ERROR");
	   }
	 }
       } else
	 ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "Brigade error");
       apr_brigade_cleanup(bb);
     } while (!end && (status == APR_SUCCESS));

     if (status != APR_SUCCESS) {
       ap_log_error(APLOG_MARK, APLOG_CRIT, 0, r->server, "error reading post input filters bucket brigade");
       ap_rprintf(r, "<p>Error reading request body after input filters.</p>");
       return OK;
     }
     GET = upload_form(r);
     //     if (NULL != GET) apr_table_do(tab_cb, r->server, GET, NULL);

     {
       char *dirname = apr_pstrcat(r->pool,
				   (upload_config->dir_field != NULL) ? upload_config->dir_field : "/tmp",
				   "/",
				   apr_table_get(GET, (upload_config->folder_field != NULL) ? upload_config->folder_field : "p_folder"),
				   NULL);
       char *filename = apr_pstrcat(r->pool,
				   dirname,
				   "/",
				   apr_table_get(GET, (upload_config->file_field != NULL) ? upload_config->file_field : "p_file"),
				   NULL);
       if (APR_SUCCESS != (status = apr_dir_make_recursive(dirname,
							   APR_UREAD | APR_UWRITE | APR_UEXECUTE |
							   APR_GREAD | APR_GEXECUTE | APR_WREAD | APR_WEXECUTE,
							   r->pool))) {
	 ap_log_error(APLOG_MARK, APLOG_CRIT, 0, r->server, "error making recursive directory %s -- %s", dirname,
		      apr_strerror(status, error_buf, sizeof(error_buf)));
	 return DECLINED;
       }
       if (APR_SUCCESS != (status = apr_file_rename(temp_filename, filename, r->pool))) {
	 ap_log_error(APLOG_MARK, APLOG_CRIT, 0, r->server, "error renaming %s into %s -- %s",
		      temp_filename, filename, apr_strerror(status, error_buf, sizeof(error_buf)));
	 return DECLINED;
       }
       if (APR_SUCCESS != (status = apr_file_perms_set(filename, UPLOADED_PERMS))) {
	 ap_log_error(APLOG_MARK, APLOG_CRIT, 0, r->server, "error changing permissions for %s -- %s",
		      filename, apr_strerror(status, error_buf, sizeof(error_buf)));
	 return DECLINED;
       }
     }
   }

   ap_log_error(APLOG_MARK, APLOG_CRIT, 0, r->server, " r->path_info: %s", r->path_info);
   //   ap_log_error(APLOG_MARK, APLOG_CRIT, 0, r->server, " r->filename: %s", r->filename);
   pathelts = NULL;
   if (APR_SUCCESS != (status = apr_filepath_list_split_impl(&pathelts, r->path_info + 1, '/', r->pool))) {
     ap_log_error(APLOG_MARK, APLOG_CRIT, 0, r->server, "error parsing path_info %s -- %s",
		  r->path_info, apr_strerror(status, error_buf, sizeof(error_buf)));
     return DECLINED;
   }

   if (pathelts->nelts > 256) {
     ap_log_error(APLOG_MARK, APLOG_CRIT, 0, r->server, "Too many subdirectories in path_info %s -- %d (max:256)",
		  r->path_info, pathelts->nelts);
     return DECLINED;
   }
   
   for (i = 0; i < pathelts->nelts; i++) {
     ap_log_error(APLOG_MARK, APLOG_CRIT, 0, r->server, " elts[%d]: %s", i, ((char**)pathelts->elts)[i]);
     if (i) {
       cursor_values[1+i] = ((char**)pathelts->elts)[i];
       cursor_value_lengths[1+i] = strlen(cursor_values[1+i]);
     }
   }
   
   //   requested_file = apr_pstrdup (r -> pool, r -> path_info /*filename*/);
   requested_file = apr_pstrdup (r -> pool, ((char**)pathelts->elts)[0]);
   i = strlen(requested_file) - 1;

   while (i > 0)
   {
     if (requested_file[i] == '.') filename_length = i;
      if (requested_file[i] == '/') break;
      i--;
   }

   if (i >= 0) {
     requested_file += i; /* now pointing to foo.ag instead of /var/www/.../foo.ag */
     if (filename_length > i) filename_length -= i;
   }

   allowed_to_serve = false;

   for (i = 0; i < config->allowed_count; i++)
   {
      if (!strcmp(config->allowed[i], requested_file))
      {
         allowed_to_serve = true;
         break;
      }
   }
   if (config->allowed_count == 0) allowed_to_serve = true;

   if (!allowed_to_serve)
   {
      ap_set_content_type(r, "text/plain");
      ap_rprintf(r, "Hello there\nThis is AG\nEnabled: %s\n", config->is_enabled ? "On" : "Off");
      ap_rprintf(r, "Requested: %s\n", requested_file);
      ap_rprintf(r, "Allowed: %s\n", allowed_to_serve ? "Yes" : "No");

      return OK; /* pretending we have served the file, may return HTTP_FORDIDDEN in the future */
   }

   if (filename_length == 0) {
     basename = requested_file;
   } else {
     basename = apr_pstrndup(r->pool, requested_file, filename_length); 
   }

   ap_args_to_table(r, &GETargs);
   if (OK != ap_parse_form_data(r, NULL, &POST, -1, (~((apr_size_t)0)))) {
     __(r->server, " ** ap_parse_form_data is NOT OK");
   }
   GET = (NULL == GET) ? GETargs : apr_table_overlay(r->pool, GETargs, GET);

   //      apr_table_do(tab_cb, r->server, GET, NULL);
   //      __(r->server, " && request_args: %s", request_args);
   
   // move all POST parameters into GET table
   {
     ap_form_pair_t *pair;
     char *buffer;
     apr_off_t len;
     apr_size_t size;
     while (NULL != (pair = apr_array_pop(POST))) {
       apr_brigade_length(pair->value, 1, &len);
       size = (apr_size_t) len;
       buffer = apr_palloc(r->pool, size + 1);
       apr_brigade_flatten(pair->value, buffer, &size);
       buffer[len] = 0;
       apr_table_setn(GET, apr_pstrdup(r->pool, pair->name), buffer); //should name and value be ap_unescape_url() -ed?
       //       __(r->server, "POST[%s]: %s", pair->name, buffer);
     }
   }

   params.r = r;
   params.args = NULL;
   apr_table_do(tab_args, &params, GET, NULL);
   params.args = apr_pstrcat(r->pool, "&", params.args, "&", NULL);

   //   __(r->server, " && params args: %s", params.args);

   cursor_values[0] = params.args;
   cursor_value_lengths[0] = strlen(cursor_values[0]);

   /* set response content type according to configuration or to default value */
   ap_set_content_type(r, upload_config->content_type_set ? upload_config->content_type : "text/html");

   /* now connecting to Postgres, getting function output, and printing it */

   pgc = ag_pool_open (r->server);

   if (PQstatus(pgc) != CONNECTION_OK)
   {
      spit_pg_error_syslog ("connect");
      ag_pool_close(r->server, pgc);
      return OK;
   }

   if (config->ServerName == NULL) {
     char *update_command;
     struct utsname node;
     config->ServerName = apr_pstrdup(config->pool, r->server->server_hostname);

     update_command = apr_psprintf(r->pool, "UPDATE system_parameters SET string_value='%s', integer_value=%ld WHERE id=2", config->ServerName, gethostid());

     pgr = PQexec (pgc, update_command);
     if (PQresultStatus(pgr) != PGRES_COMMAND_OK) {
       spit_pg_error_syslog ("update system_parameters id=2");
       return clean_up_connection(r->server);
     }
     PQclear (pgr);

     if (0 == uname(&node)) {
       update_command = apr_psprintf(r->pool, "UPDATE system_parameters SET string_value='%s', integer_value=%ld WHERE id=6", node.nodename, gethostid());
       pgr = PQexec (pgc, update_command);
       if (PQresultStatus(pgr) != PGRES_COMMAND_OK) {
	 spit_pg_error_syslog ("update system_parameters id=6");
	 return clean_up_connection(r->server);
       }
       PQclear (pgr);
     } else {
       update_command = apr_psprintf(r->pool, "uname() failed, %d -- %s; node_name is not updated", errno, strerror(errno));
       spit_pg_error_syslog (update_command);
     }
   }

   /* removing extention (.ag or other) from file name, and adding "ag_" for function name, i.e. foo.ag becomes ag_foo() */
   cursor_string = apr_psprintf(r -> pool,
				"select * from ag_%s($1::varchar, (select id from users where username=$2), ARRAY[",
				basename);
   
   for (i = 1; i < pathelts->nelts; i++) {
     cursor_string = apr_pstrcat(r->pool, cursor_string,
				 (i > 1) ? "," : apr_psprintf(r->pool, "$%d", i + 2),
				 (i > 1) ? apr_psprintf(r->pool, "$%d", i + 2) : NULL,
				 NULL);
   }

   cursor_string = apr_pstrcat(r->pool, cursor_string, "]::text[])", NULL);
   
     
   /* passing GET as first (and only) parameter */
   if (0 == PQsendQueryParams (pgc, cursor_string, 1 + pathelts->nelts, NULL, cursor_values, cursor_value_lengths, cursor_value_formats, 0)) {
      spit_pg_error_syslog ("sending async query with params");
      return clean_up_connection(r->server);
   }

   if (0 == PQsetSingleRowMode(pgc)) {
     ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "can not fall into single raw mode to fetch data");
   }
   
   while (NULL != (pgr = PQgetResult(pgc))) {

     if (PQresultStatus(pgr) != PGRES_TUPLES_OK && PQresultStatus(pgr) != PGRES_SINGLE_TUPLE) {
       spit_pg_error_syslog ("fetch data");
       return clean_up_connection(r->server);
     }

     /* the following counts and for-loop may seem excessive as it's just 1 row/1 field, but might need it in the future */

     field_count = PQnfields(pgr);
     tuple_count = PQntuples(pgr);

     for (i = 0; i < tuple_count; i++)
       {
	 for (j = 0; j < field_count; j++) ap_rprintf(r, "%s", PQgetvalue(pgr, i, j));
	 ap_rprintf(r, "\n");
       }
     PQclear (pgr);
   }
   ag_pool_close(r->server, pgc);

   return OK;
}

static apr_status_t init_db_pool(apr_pool_t* p, apr_pool_t* plog, apr_pool_t* ptemp) {
  apr_status_t rc = APR_SUCCESS;

  rhash_library_init();
  ag_pool_config = apr_hash_make(p);
  return rc;
}


static void tmpfile_cleanup(apr_file_t *tmpfile) {
  apr_finfo_t tinfo;

  apr_file_datasync(tmpfile) ;
  if (APR_SUCCESS == apr_file_info_get(&tinfo, APR_FINFO_SIZE, tmpfile)) {
    apr_file_trunc(tmpfile, tinfo.size - 2); /* remove extra \r\n */
  }

  apr_file_close(tmpfile) ;
}



static apr_status_t tmpfile_filter(ap_filter_t *f, apr_bucket_brigade *bbout,
	ap_input_mode_t mode, apr_read_type_e block, apr_off_t nbytes) {

  ag_dir_cfg *config = (ag_dir_cfg*) ap_get_module_config(f->r->per_dir_config, &ag_module);
  apr_bucket_brigade* bbin = apr_brigade_create(f->r->pool, f->r->connection->bucket_alloc);
  apr_file_t* tmpfile = NULL;
  const char* ctype = apr_table_get(f->r->headers_in, "Content-Type") ;
  const char* cl_header = apr_table_get(f->r->headers_in, "Content-Length");
  char* tmpname = apr_pstrcat(f->r->pool,
			      (config->dir_field != NULL) ? config->dir_field : "/tmp",
			      "/mod-ag.XXXXXX", NULL) ;
  apr_off_t readbytes = (cl_header) ? (apr_off_t) apr_atoi64(cl_header) : BUFLEN;

#if defined DEBUG
  ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "tmpfile_filter: cl_header:%s  readbytes:%lu  nbytes:%lu", cl_header, readbytes, nbytes) ;
#endif

  if ( f->ctx ) {
    APR_BRIGADE_INSERT_TAIL(bbout, apr_bucket_eos_create(bbout->bucket_alloc)) ;
    return APR_SUCCESS ;
  }
  
  for ( ; ; ) {
    apr_bucket* b ;
    const char* ptr = 0 ;
    apr_size_t bytes ;
#if defined DEBUG
    ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "get_brigade") ;
#endif
    ap_get_brigade(f->next, bbin, AP_MODE_READBYTES, APR_BLOCK_READ, readbytes) ;

    for ( b = APR_BRIGADE_FIRST(bbin) ;
	  b != APR_BRIGADE_SENTINEL(bbin) && ! f->ctx ;
	  b = APR_BUCKET_NEXT(b) ) {

      if ( APR_BUCKET_IS_EOS(b) ) {
	f->ctx = f ;	// just using it as a flag; any nonzero will do
	if (NULL != tmpfile) apr_file_flush(tmpfile) ;
	if (APR_SUCCESS != apr_brigade_puts(bbout, ap_filter_flush, f, tmpname)) {
	  __(f->r->server, " SENDING tmpname FAILED!!!");
	}
	/*	apr_brigade_puts(bbout, ap_filter_flush, f, tmpname) ;*/
	APR_BRIGADE_INSERT_TAIL(bbout,
				apr_bucket_eos_create(bbout->bucket_alloc) ) ;
      } else if ( APR_BUCKET_IS_METADATA(b) ) {
	apr_bucket *metadata_bucket;
	metadata_bucket = b;
	b = APR_BUCKET_NEXT(b);
	APR_BUCKET_REMOVE(metadata_bucket);
	APR_BRIGADE_INSERT_TAIL(bbout, metadata_bucket);
	if (b == APR_BRIGADE_SENTINEL(bbin)) break;
      } else if ( apr_bucket_read(b, &ptr, &bytes, APR_BLOCK_READ)
		  == APR_SUCCESS ) {
#if defined DEBUG
	ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "tmpfile_filter: %ld bytes in bucket", bytes) ;
#endif
	if (NULL == tmpfile) {
	  if ( apr_file_mktemp(&tmpfile, tmpname, KEEPONCLOSE, f->r->pool) != APR_SUCCESS ) {
	    // error
	    ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "Can not create file %s", tmpname) ;
	    ap_remove_input_filter(f) ;
	  }
	  apr_pool_cleanup_register(f->r->pool, tmpfile,
				    (void*)tmpfile_cleanup, apr_pool_cleanup_null) ;

	}
	apr_file_write(tmpfile, ptr, &bytes) ;
	apr_file_flush(tmpfile) ;
      } else {
	__(f->r->server, " apr_bucket_read != APR_SUCCESS");
      }
    }
    if ( f->ctx ) {
      break ;
    }
    else
	  apr_brigade_cleanup(bbin) ;
  }

  apr_brigade_destroy(bbin) ;

  return APR_SUCCESS ;
}


static char* lccopy(apr_pool_t* p, const char* str) {
  char* ret = apr_pstrdup(p, str) ;
  char* q ;
  for ( q = ret ; *q ; ++q )
    if ( isupper(*q) )
      *q = tolower(*q) ;
  return ret ;
}

static char* get_boundary(apr_pool_t* p, const char* ctype) {
  char* ret = NULL ;
  if ( ctype ) {
    char* lctype = lccopy(p, ctype) ;
    char* bdy = strstr(lctype, "boundary") ;
    if ( bdy ) {
      char* ptr = strchr(bdy, '=') ;
      if ( ptr ) {
	bdy = (char*) ctype + ( ptr - lctype ) + 1 ;
	for ( ptr = bdy; *ptr; ++ptr )
	  if ( *ptr == ';' || isspace(*ptr) )
	    *ptr = 0 ;
	ret = apr_pstrdup(p, bdy) ;
      }
    }
  }
  return ret ;
}

static void set_header(upload_ctx* ctx, const char* data) {
  char* colon = strchr(data, ':' ) ;
  if ( colon ) {
    *colon++ = 0 ;
    while ( isspace(*colon) )
      ++colon ;
    if ( ! strcasecmp( data, "Content-Disposition" ) ) {
      char* np = strstr(colon, "name=") ;
      //      if ( np )
      //	np = strstr(colon, "name=\"") ;
      if ( np ) {
	char* ep ;
	np += 6 ;
	ep = strchr(np, (int)'"') ;
	if ( ep ) {
	  //	  *ep = 0 ;
	  ctx->key = apr_pstrndup(ctx->pool, np, ep-np) ;
	  if ( ! strcasecmp(ctx->key, ctx->file_field) ) {
	    char *fnp;
	    //	    *ep = '"';
	    fnp = strstr(colon, "filename=");
	    //	    if ( fnp )
	    //	      fnp = strstr(colon, "filename=\"");
	    if (fnp) {
	      char *ep;
	      fnp += 10;
	      ep = strchr(fnp, (int)'"') ;
	      if (ep) {
		//		*ep = 0;
		ctx->val = apr_pstrndup(ctx->pool, fnp, ep - fnp) ;
	      }
	    }
	    
	    ctx->is_file = 1 ;
	  }
	}
      }
    }
  }
}

static void set_body(upload_ctx* ctx, const char* data) {
  const char* cr = strchr(data, '\r') ;
  char* tmp = apr_pstrndup(ctx->pool, data, cr-data) ;
  if ( ctx->val )
    ctx->val = apr_pstrcat(ctx->pool, ctx->val, tmp, NULL) ;
  else
    ctx->val = tmp ;
}

static enum { boundary_part, boundary_end, boundary_none }
is_boundary( upload_ctx* ctx, const char* p) {
    size_t blen = strlen(ctx->boundary) ;
    if ( strlen(p) < 2 + blen )
      return boundary_none ;
    if ( ( p[0] != '-' ) || ( p[1] != '-' ) )
      return boundary_none ;
    if ( strncmp(ctx->boundary, p+2,  blen ) )
      return boundary_none ;
    if ( ( p[blen+2] == '-' ) && ( p[blen+3] == '-' ) )
      return boundary_end ;
    else
      return boundary_part ;
}

static void end_body(upload_ctx* ctx) {
  if ( ! ctx->is_file ) {
    apr_table_set(ctx->form, ctx->key, ctx->val) ;
  }
  else {
    apr_table_set(ctx->form, ctx->key, ctx->val) ;
    ctx->is_file = 0 ;
  }

  ctx->key = ctx->val = 0 ;
}


static apr_status_t upload_filter_init(ap_filter_t* f) {
  ag_dir_cfg* conf = (ag_dir_cfg*)ap_get_module_config(f->r->per_dir_config, &ag_module);
  upload_ctx* ctx = apr_palloc(f->r->pool, sizeof(upload_ctx)) ;
  // check content-type, get boundary or error
  const char* ctype = apr_table_get(f->r->headers_in, "Content-Type") ;

  if ( ! ctype || ! conf->file_field ||
	strncmp ( ctype , "multipart/form-data", 19 ) ) {
    ap_remove_input_filter(f) ;
    return APR_SUCCESS ;
  }

  ctx->pool = f->r->pool ;
  ctx->server = f->r->server;
  ctx->form = apr_table_make(ctx->pool, conf->form_size) ;
  ctx->boundary = get_boundary(f->r->pool, ctype) ;
  ctx->parse_state = p_none ;
  ctx->key = ctx->val = 0 ;
  ctx->file_field = conf->file_field ;
  ctx->is_file = 0 ;
  ctx->leftover = 0 ;

  /* save the table in request_config */
  ap_set_module_config(f->r->request_config, &ag_module, ctx->form) ;

  f->ctx = ctx ;
  return APR_SUCCESS ;
}


static apr_status_t upload_filter(ap_filter_t *f, apr_bucket_brigade *bbout,
	ap_input_mode_t mode, apr_read_type_e block, apr_off_t nbytes) {

  char* buf = 0 ;
  char* p = buf ;
  char* e ;
  const char* cl_header = apr_table_get(f->r->headers_in, "Content-Length");
 
  int ret = APR_SUCCESS ;
 
  apr_size_t bytes = 0 ;
  apr_off_t readbytes = (cl_header) ? (apr_off_t) apr_atoi64(cl_header) : nbytes;
  apr_bucket* b ;
  apr_bucket_brigade* bbin ;
 
  upload_ctx* ctx = (upload_ctx*) f->ctx ;

#if defined DEBUG
  ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "upload_filter: cl_header:%s  readbytes:%lu  nbytes:%lu", cl_header, readbytes, nbytes) ;
#endif

    if ( ctx->parse_state == p_done ) {
    // send an EOS
    APR_BRIGADE_INSERT_TAIL(bbout, apr_bucket_eos_create(bbout->bucket_alloc) ) ;
    return APR_SUCCESS ;
  }

  /* should be more efficient to do this in-place without resorting
   * to a new brigade
   */
  bbin = apr_brigade_create(f->r->pool, f->r->connection->bucket_alloc) ;

  if ( ret = ap_get_brigade(f->next, bbin, mode, block, readbytes) ,
	ret != APR_SUCCESS )
     return ret ;


  for ( b = APR_BRIGADE_FIRST(bbin) ;
	b != APR_BRIGADE_SENTINEL(bbin) ;
	b = APR_BUCKET_NEXT(b) ) {
    const char* ptr = buf ;
    if ( APR_BUCKET_IS_EOS(b) ) {
      ctx->parse_state = p_done ;
      APR_BRIGADE_INSERT_TAIL(bbout,
	 apr_bucket_eos_create(bbout->bucket_alloc) ) ;
      apr_brigade_destroy(bbin) ;
      return APR_SUCCESS ;
    } else if ( apr_bucket_read(b, &ptr, &bytes, APR_BLOCK_READ)
		== APR_SUCCESS ) {
      const char* p = ptr ;
#if defined DEBUG
      ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "upload_filter: %ld bytes in bucket", bytes) ;
#endif
      if (ctx->leftover) {
	apr_size_t new_bytes ;
#if defined DEBUG
	ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "upload_filter: leftover %ld bytes", ctx->leftsize) ;
#endif
	p = apr_pmemcat(f->r->pool, &new_bytes, ctx->leftover, ctx->leftsize, ptr, bytes, NULL) ;
	ctx->leftover = NULL ;
	ctx->leftsize = 0 ;
	bytes = new_bytes ;
	ptr = p;
#if defined DEBUG
	ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "upload_filter: new size %ld bytes", bytes) ;
#endif
      }
      while ( e = memchr(p, (int)'\n', bytes - (p - ptr)), ( e && ( e < (ptr+bytes) ) ) ) {
	const char* ptmp = p ;
	apr_size_t ptmplen = (e - p);
	int hasr = (ptmplen && ptmp[ptmplen - 1] == '\r') ? 1 : 0 ;
	*e = 0 ;
	if (hasr) ptmplen--,*(--e) = 0 ;
	switch ( ctx->parse_state ) {
	  case p_none:
#if defined DEBUG
	    ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "upload_filter: parse state: p_none") ;
#endif
	    if ( is_boundary(ctx, ptmp) == boundary_part )
	      ctx->parse_state = p_head ;
	    break ;
	  case p_head:
#if defined DEBUG
	    ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "upload_filter: parse state: p_head") ;
#endif
	    if ( (! *ptmp) || ( *ptmp == '\r') ) {
#if defined DEBUG
	      ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "upload_filter: parse state: >p_field") ;
#endif
	      ctx->parse_state = p_field ;
	    } else
	      set_header(ctx, ptmp) ;
	    break ;
	  case p_field:
#if defined DEBUG
	    ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "upload_filter: parse state: p_field") ;
#endif
	    switch ( is_boundary(ctx, ptmp) ) {
	      case boundary_part:
		end_body(ctx) ;
		ctx->parse_state = p_head ;
		break ;
	      case boundary_end:
		end_body(ctx) ;
		ctx->parse_state = p_end ;
		break ;
	      case boundary_none:
		if ( ctx->is_file ) {
		  /*		  apr_brigade_puts(bbout, ap_filter_flush, f, ptmp) ;
				  apr_brigade_putc(bbout, ap_filter_flush, f, '\n') ;*/
		  apr_brigade_write(bbout, NULL, NULL, ptmp, ptmplen) ;
		  if (hasr) apr_brigade_putc(bbout, NULL, NULL, '\r') ;
		  apr_brigade_putc(bbout, NULL, NULL, '\n') ;
		} else
		  set_body(ctx, ptmp) ;
		break ;
	    }
	    break ;
	  case p_end:
#if defined DEBUG
	    ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "upload_filter: parse state: p_end") ;
#endif
	    //APR_BRIGADE_INSERT_TAIL(bbout,
	//	apr_bucket_eos_create(bbout->bucket_alloc) ) ;
	    ctx->parse_state = p_done ;
	  case p_done:
#if defined DEBUG
	    ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "upload_filter: parse state: p_done") ;
#endif
	    break ;
	}
#if defined DEBUG
	ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "upload_filter: e - ptr: %ld  bytes:%ld  hasr:%d", (e - ptr), bytes, hasr) ;
#endif
	if ( e - ptr >= bytes - hasr )
	  break ;
	p = e + 1 ;
	if (hasr) p++;
      }
#if defined DEBUG
      ap_log_rerror(APLOG_MARK,APLOG_CRIT,0, f->r, "upload_filter: while ends") ;
#endif
      if ( ( ctx->parse_state != p_end ) && ( ctx->parse_state != p_done ) ) {
	size_t bleft = bytes - (p-ptr) ;
	ctx->leftover = apr_pmemdup(f->r->pool, p, bleft ) ;
	ctx->leftsize = bleft;
      }
    }
  }
  apr_brigade_destroy(bbin) ;
  return ret ;
}


static int syslog_init(apr_pool_t* p, apr_pool_t* plog,
	apr_pool_t* ptemp, server_rec* s) {

  void *data = NULL;
  char *key;
  const char *userdata_key = "jl-syslog_post_config";

  // This code is used to prevent double initialization of the module during Apache startup
  apr_pool_userdata_get(&data, userdata_key, s->process->pool);
  if ( data == NULL ) {
    apr_pool_userdata_set((const void *)1, userdata_key, apr_pool_cleanup_null, s->process->pool);
    return OK;
  }

  openlog("purgora", 0, LOG_USER);
  return OK;
}


int syslog_level(const char *priority) {
  if (!strcasecmp(priority, "warn")) return LOG_WARNING;
  if (!strcasecmp(priority, "error")) return LOG_ERR;
  if (!strcasecmp(priority, "info")) return LOG_INFO;

  return LOG_DEBUG;
}



static int syslog_handler (request_rec *r) {
   const char * log_text = NULL;
   const char *priority = r->path_info;
   apr_off_t log_size;
   if (!r -> handler || strcmp (r -> handler, "jl-syslog") ) return DECLINED;
   if (!r -> method || (strcmp (r -> method, "GET") && strcmp (r -> method, "POST")) ) return DECLINED;
   if (NULL == r->user) return DECLINED;

   if (OK != util_read(r, &log_text, &log_size)) {
     ap_log_error(APLOG_MARK, APLOG_CRIT, 0, r->server, "error reading POST body");
     return DECLINED;
   }
   while(*priority == '/') priority++;

   __(r->server, "[SYSLOG][%s][%s][%s]:%s",
      ap_get_remote_host(r->connection, r->per_dir_config, REMOTE_NOLOOKUP, NULL), r->user, priority, log_text);

   syslog(syslog_level(priority), "[%s:%s]:%s",
	  ap_get_remote_host(r->connection, r->per_dir_config, REMOTE_NOLOOKUP, NULL),
	  r->user, log_text);

   ap_set_content_type(r, "text/plain");
   ap_rprintf(r, "Logged.");

   return OK;
}

/* AUTH */

static void note_cookie_auth_failure(request_rec * r) {
    ag_dir_cfg *conf = ap_get_module_config(r->per_dir_config, &ag_module);

    if (conf->location && ap_strchr_c(conf->location, ':')) {
        apr_table_setn(r->err_headers_out, "Location", conf->location);
    }
}

static int hook_note_cookie_auth_failure(request_rec * r,
                                         const char *auth_type) {
    if (strcasecmp(auth_type, "purgora"))
        return DECLINED;

    note_cookie_auth_failure(r);
    return OK;
}

/**
 * Set the auth username and password into the main request
 * notes table.
 */
static void set_notes_auth(request_rec * r,
                                const char *user, const char *pw,
                                const char *method, const char *mimetype) {
    apr_table_t *notes = NULL;
    const char *authname;

    /* find the main request */
    while (r->main) {
        r = r->main;
    }
    /* find the first redirect */
    while (r->prev) {
        r = r->prev;
    }
    notes = r->notes;

    /* have we isolated the user and pw before? */
    authname = ap_auth_name(r);
    if (user) {
        apr_table_setn(notes, apr_pstrcat(r->pool, authname, "-user", NULL), user);
    }
    if (pw) {
        apr_table_setn(notes, apr_pstrcat(r->pool, authname, "-pw", NULL), pw);
    }
    if (method) {
        apr_table_setn(notes, apr_pstrcat(r->pool, authname, "-method", NULL), method);
    }
    if (mimetype) {
        apr_table_setn(notes, apr_pstrcat(r->pool, authname, "-mimetype", NULL), mimetype);
    }

}

/**
 * Get the auth username and password from the main request
 * notes table, if present.
 */
static void get_notes_auth(request_rec *r,
                           const char **user, const char **pw,
                           const char **method, const char **mimetype) {
    const char *authname;
    request_rec *m = r;

    /* find the main request */
    while (m->main) {
        m = m->main;
    }
    /* find the first redirect */
    while (m->prev) {
        m = m->prev;
    }

    /* have we isolated the user and pw before? */
    authname = ap_auth_name(m);
    if (user) {
        *user = (char *) apr_table_get(m->notes, apr_pstrcat(m->pool, authname, "-user", NULL));
    }
    if (pw) {
        *pw = (char *) apr_table_get(m->notes, apr_pstrcat(m->pool, authname, "-pw", NULL));
    }
    if (method) {
        *method = (char *) apr_table_get(m->notes, apr_pstrcat(m->pool, authname, "-method", NULL));
    }
    if (mimetype) {
        *mimetype = (char *) apr_table_get(m->notes, apr_pstrcat(m->pool, authname, "-mimetype", NULL));
    }

    /* set the user, even though the user is unauthenticated at this point */
    if (user && *user) {
        r->user = (char *) *user;
    }

    ap_log_rerror(APLOG_MARK, APLOG_TRACE1, 0, r,
                  "from notes: user: %s, pw: %s, method: %s, mimetype: %s",
                  user ? *user : "<null>", pw ? *pw : "<null>",
                  method ? *method : "<null>", mimetype ? *mimetype : "<null>");

}

/**
 * Set the auth username and password into the session.
 *
 * If either the username, or the password are NULL, the username
 * and/or password will be removed from the session.
 */
static apr_status_t set_session_auth(request_rec * r,
                                     const char *user, const char *pw, const char *site) {
    const char *hash = NULL;
    const char *authname = ap_auth_name(r);
    session_rec *z = NULL;

    if (site) {
        hash = ap_md5(r->pool,
                      (unsigned char *) apr_pstrcat(r->pool, user, ":", site, NULL));
    }

    ap_session_load_fn(r, &z);
    ap_session_set_fn(r, z, apr_pstrcat(r->pool, authname, "-" MOD_SESSION_USER, NULL), user);
    ap_session_set_fn(r, z, apr_pstrcat(r->pool, authname, "-" MOD_SESSION_PW, NULL), pw);
    ap_session_set_fn(r, z, apr_pstrcat(r->pool, authname, "-" MOD_AUTH_FORM_HASH, NULL), hash);

    return APR_SUCCESS;

}

/**
 * Get the auth username and password from the main request
 * notes table, if present.
 */
static apr_status_t get_session_auth(request_rec * r,
                                     const char **user, const char **pw, const char **hash) {
    const char *authname = ap_auth_name(r);
    session_rec *z = NULL;

    ap_session_load_fn(r, &z);

    if (user) {
        ap_session_get_fn(r, z, apr_pstrcat(r->pool, authname, "-" MOD_SESSION_USER, NULL), user);
    }
    if (pw) {
        ap_session_get_fn(r, z, apr_pstrcat(r->pool, authname, "-" MOD_SESSION_PW, NULL), pw);
    }
    if (hash) {
        ap_session_get_fn(r, z, apr_pstrcat(r->pool, authname, "-" MOD_AUTH_FORM_HASH, NULL), hash);
    }

    /* set the user, even though the user is unauthenticated at this point */
    if (user && *user) {
        r->user = (char *) *user;
    }

    ap_log_rerror(APLOG_MARK, APLOG_TRACE1, 0, r,
                  "from session: " MOD_SESSION_USER ": %s, " MOD_SESSION_PW
                  ": %s, " MOD_AUTH_FORM_HASH ": %s",
                  user ? *user : "<null>", pw ? *pw : "<null>",
                  hash ? *hash : "<null>");

    return APR_SUCCESS;

}


/**
 * Isolate the username and password in a POSTed form with the
 * username in the "username" field, and the password in the
 * "password" field.
 *
 * If either the username or the password is missing, this
 * function will return HTTP_UNAUTHORIZED.
 *
 * The location field is considered optional, and will be returned
 * if present.
 */
static int get_form_auth(request_rec * r,
                             const char *username,
                             const char *password,
                             const char *location,
                             const char *method,
                             const char *mimetype,
                             const char *body,
                             const char **sent_user,
                             const char **sent_pw,
                             const char **sent_loc,
                             const char **sent_method,
                             const char **sent_mimetype,
                             apr_bucket_brigade **sent_body,
                             ag_dir_cfg * conf)
{
    /* sanity check - are we a POST request? */

    /* find the username and password in the form */
    apr_array_header_t *pairs = NULL;
    apr_off_t len;
    apr_size_t size;
    int res;
    char *buffer;

    /* have we isolated the user and pw before? */
    get_notes_auth(r, sent_user, sent_pw, sent_method, sent_mimetype);
    if (*sent_user && *sent_pw) {
        return OK;
    }

    res = ap_parse_form_data(r, NULL, &pairs, -1, conf->max_form_size);
    if (res != OK) {
        return res;
    }
    while (pairs && !apr_is_empty_array(pairs)) {
        ap_form_pair_t *pair = (ap_form_pair_t *) apr_array_pop(pairs);
        if (username && !strcmp(pair->name, username) && sent_user) {
            apr_brigade_length(pair->value, 1, &len);
            size = (apr_size_t) len;
            buffer = apr_palloc(r->pool, size + 1);
            apr_brigade_flatten(pair->value, buffer, &size);
            buffer[len] = 0;
            *sent_user = buffer;
        }
        else if (password && !strcmp(pair->name, password) && sent_pw) {
            apr_brigade_length(pair->value, 1, &len);
            size = (apr_size_t) len;
            buffer = apr_palloc(r->pool, size + 1);
            apr_brigade_flatten(pair->value, buffer, &size);
            buffer[len] = 0;
            *sent_pw = buffer;
        }
        else if (location && !strcmp(pair->name, location) && sent_loc) {
            apr_brigade_length(pair->value, 1, &len);
            size = (apr_size_t) len;
            buffer = apr_palloc(r->pool, size + 1);
            apr_brigade_flatten(pair->value, buffer, &size);
            buffer[len] = 0;
            *sent_loc = buffer;
        }
        else if (method && !strcmp(pair->name, method) && sent_method) {
            apr_brigade_length(pair->value, 1, &len);
            size = (apr_size_t) len;
            buffer = apr_palloc(r->pool, size + 1);
            apr_brigade_flatten(pair->value, buffer, &size);
            buffer[len] = 0;
            *sent_method = buffer;
        }
        else if (mimetype && !strcmp(pair->name, mimetype) && sent_mimetype) {
            apr_brigade_length(pair->value, 1, &len);
            size = (apr_size_t) len;
            buffer = apr_palloc(r->pool, size + 1);
            apr_brigade_flatten(pair->value, buffer, &size);
            buffer[len] = 0;
            *sent_mimetype = buffer;
        }
        else if (body && !strcmp(pair->name, body) && sent_body) {
            *sent_body = pair->value;
        }
    }

    ap_log_rerror(APLOG_MARK, APLOG_TRACE1, 0, r,
                  "from form: user: %s, pw: %s, method: %s, mimetype: %s, location: %s",
                  sent_user ? *sent_user : "<null>", sent_pw ? *sent_pw : "<null>",
                  sent_method ? *sent_method : "<null>",
                  sent_mimetype ? *sent_mimetype : "<null>",
                  sent_loc ? *sent_loc : "<null>");

    /* set the user, even though the user is unauthenticated at this point */
    if (sent_user && *sent_user) {
        r->user = (char *) *sent_user;
    }

    /* a missing username or missing password means auth denied */
    if (!sent_user || !*sent_user) {

        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "form parsed, but username field '%s' was missing or empty, unauthorized",
                      username);

        return HTTP_UNAUTHORIZED;
    }
    if (!sent_pw || !*sent_pw) {

        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "form parsed, but password field '%s' was missing or empty, unauthorized",
                      password);

        return HTTP_UNAUTHORIZED;
    }

    /*
     * save away the username, password, mimetype and method, so that they
     * are available should the auth need to be run again.
     */
    set_notes_auth(r, *sent_user, *sent_pw, sent_method ? *sent_method : NULL,
                   sent_mimetype ? *sent_mimetype : NULL);

    return OK;
}

/* These functions return 0 if client is OK, and proper error status
 * if not... either HTTP_UNAUTHORIZED, if we made a check, and it failed, or
 * HTTP_INTERNAL_SERVER_ERROR, if things are so totally confused that we
 * couldn't figure out how to tell if the client is authorized or not.
 *
 * If they return DECLINED, and all other modules also decline, that's
 * treated by the server core as a configuration error, logged and
 * reported as such.
 */


/**
 * Given a username and site passphrase hash from the session, determine
 * whether the site passphrase is valid for this session.
 *
 * If the site passphrase is NULL, or if the sent_hash is NULL, this
 * function returns DECLINED.
 *
 * If the site passphrase hash does not match the sent hash, this function
 * returns AUTH_USER_NOT_FOUND.
 *
 * On success, returns OK.
 */
static int check_site(request_rec * r, const char *site, const char *sent_user, const char *sent_hash)
{

    if (site && sent_user && sent_hash) {
        const char *hash = ap_md5(r->pool,
                      (unsigned char *) apr_pstrcat(r->pool, sent_user, ":", site, NULL));

        if (!strcmp(sent_hash, hash)) {
            return OK;
        }
        else {
            return AUTH_USER_NOT_FOUND;
        }
    }

    return DECLINED;

}

/**
 * Given a username and password (extracted externally from a cookie), run
 * the authnz hooks to determine whether this request is authorized.
 * store start page location if set.
 *
 * Return an HTTP code.
 */
static int check_authn(request_rec * r, const char *sent_user, const char *sent_pw, const char **sent_loc) {
  authn_status auth_result;
  ag_dir_cfg *conf = ap_get_module_config(r->per_dir_config, &ag_module);
  PGconn * pgc;
  PGresult * pgr;
  int tuple_count;
  int i, res;
  char *message;
  char digest[65];
  char digest_print[1030];


  if (sent_loc != NULL) *sent_loc = NULL;
  if (!sent_user || !sent_pw) {
    auth_result = AUTH_USER_NOT_FOUND;
  } else {
    apr_table_setn(r->notes, AUTHN_PROVIDER_NAME_NOTE, AUTHN_PURGORA_PROVIDER);
    //      auth_result = provider->check_password(r, sent_user, sent_pw);
    pgc = ag_pool_open(r->server);
    switch ((pgc != NULL) ? true : false) {
    case false:
      spit_pg_error_syslog("connect for auth");
      auth_result = AUTH_GENERAL_ERROR;
      break;
    case true:
      pgr = PQexec(pgc, apr_psprintf(r->pool,
				     "select u.id, username, password, coalesce(case when start_page !~ '^[0-9]$' then page_file else NULL end, case when u.start_page ~ '^[0-9]+$' then '/p/dashboard_page?p_dashboard_id=' || u.start_page else NULL end) as location from users u left join lu_page_names pn ON pn.code = u.start_page where coalesce(u.start_date, now()) <= now() and coalesce(u.end_date, now()) >= now() and username = '%s'",
				     sent_user)
		   );
      if (PQresultStatus(pgr) != PGRES_TUPLES_OK) {
	spit_pg_error_syslog ("requesting users");
	ag_pool_close(r->server, pgc);
	auth_result = AUTH_GENERAL_ERROR;
	PQclear (pgr);
	break;
      }
      auth_result = AUTH_USER_NOT_FOUND;
      tuple_count = PQntuples(pgr);
      //      __(r->server, " ** %d tuples for user '%s'", tuple_count, sent_user);
      for (i = 0; i < tuple_count; i++) {
	message = apr_pstrcat(r->pool,  PQgetvalue(pgr, i, 0), sent_pw, NULL);
	res = rhash_msg(RHASH_SHA3_512, message, strlen(message), digest);
	if (res < 0) {
	  ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server, "SHA3_512 hash calculation error %d", res) ;
	  auth_result = AUTH_GENERAL_ERROR;
	  PQclear (pgr);
	  break;
	}
	rhash_print_bytes(digest_print, digest, rhash_get_digest_size(RHASH_SHA3_512), RHPR_HEX);
	if (0 == strcmp(digest_print, PQgetvalue(pgr, i, 2))) {
	  auth_result = AUTH_GRANTED;
	  if (sent_loc != NULL) {
	    *sent_loc = apr_pstrdup(r->pool, PQgetvalue(pgr, i, 3));
	  }
	  break;
	} else auth_result = AUTH_DENIED;
      }
      PQclear (pgr);
      break;
    }
    ag_pool_close(r->server, pgc);
    apr_table_unset(r->notes, AUTHN_PROVIDER_NAME_NOTE);
  }

    if (auth_result != AUTH_GRANTED) {
        int return_code;

        /* If we're not authoritative, then any error is ignored. */
        if (!(conf->authoritative) && auth_result != AUTH_DENIED) {
            return DECLINED;
        }

        switch (auth_result) {
        case AUTH_DENIED:
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(01807)
                          "user '%s': authentication failure for \"%s\": "
                          "password Mismatch",
                          sent_user, r->uri);
            return_code = HTTP_UNAUTHORIZED;
            break;
        case AUTH_USER_NOT_FOUND:
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(01808)
                          "user '%s' not found: %s", sent_user, r->uri);
            return_code = HTTP_UNAUTHORIZED;
            break;
        case AUTH_GENERAL_ERROR:
        default:
            /*
             * We'll assume that the module has already said what its error
             * was in the logs.
             */
            return_code = HTTP_INTERNAL_SERVER_ERROR;
            break;
        }

        /* If we're returning 403, tell them to try again. */
        if (return_code == HTTP_UNAUTHORIZED) {
            note_cookie_auth_failure(r);
        }

/* TODO: Flag the user somehow as to the reason for the failure */

        return return_code;
    }

    return OK;

}

/* fake the basic authentication header if configured to do so */
static void fake_basic_authentication(request_rec *r, ag_dir_cfg *conf,
                                      const char *user, const char *pw)
{
    if (conf->fakebasicauth) {
        char *basic = apr_pstrcat(r->pool, user, ":", pw, NULL);
        apr_size_t size = (apr_size_t) strlen(basic);
        char *base64 = apr_palloc(r->pool,
                                  apr_base64_encode_len(size + 1) * sizeof(char));
        apr_base64_encode(base64, basic, size);
        apr_table_setn(r->headers_in, "Authorization",
                       apr_pstrcat(r->pool, "Basic ", base64, NULL));
    }
}


/*****************************************************************
 *
 * The sub_request mechanism.
 *
 * Fns to look up a relative URI from, e.g., a map file or SSI document.
 * These do all access checks, etc., but don't actually run the transaction
 * ... use run_sub_req below for that.  Also, be sure to use destroy_sub_req
 * as appropriate if you're likely to be creating more than a few of these.
 * (An early Apache version didn't destroy the sub_reqs used in directory
 * indexing.  The result, when indexing a directory with 800-odd files in
 * it, was massively excessive storage allocation).
 *
 * Note more manipulation of protocol-specific vars in the request
 * structure...
 */

static request_rec *make_sub_request(const request_rec *r,
                                     ap_filter_t *next_filter)
{
    apr_pool_t *rrp;
    request_rec *rnew;

    apr_pool_create(&rrp, r->pool);
    apr_pool_tag(rrp, "subrequest");
    rnew = apr_pcalloc(rrp, sizeof(request_rec));
    rnew->pool = rrp;

    rnew->hostname       = r->hostname;
    rnew->request_time   = r->request_time;
    rnew->connection     = r->connection;
    rnew->server         = r->server;
    rnew->log            = r->log;

    rnew->request_config = ap_create_request_config(rnew->pool);

    /* Start a clean config from this subrequest's vhost.  Optimization in
     * Location/File/Dir walks from the parent request assure that if the
     * config blocks of the subrequest match the parent request, no merges
     * will actually occur (and generally a minimal number of merges are
     * required, even if the parent and subrequest aren't quite identical.)
     */
    rnew->per_dir_config = r->server->lookup_defaults;

    rnew->htaccess = r->htaccess;
    rnew->allowed_methods = ap_make_method_list(rnew->pool, 2);

    /* make a copy of the allowed-methods list */
    ap_copy_method_list(rnew->allowed_methods, r->allowed_methods);

    /* start with the same set of output filters */
    if (next_filter) {
        /* while there are no input filters for a subrequest, we will
         * try to insert some, so if we don't have valid data, the code
         * will seg fault.
         */
        rnew->input_filters = r->input_filters;
        rnew->proto_input_filters = r->proto_input_filters;
        rnew->output_filters = next_filter;
        rnew->proto_output_filters = r->proto_output_filters;
        ap_add_output_filter_handle(ap_subreq_core_filter_handle,
                                    NULL, rnew, rnew->connection);
    }
    else {
        /* If NULL - we are expecting to be internal_fast_redirect'ed
         * to this subrequest - or this request will never be invoked.
         * Ignore the original request filter stack entirely, and
         * drill the input and output stacks back to the connection.
         */
        rnew->proto_input_filters = r->proto_input_filters;
        rnew->proto_output_filters = r->proto_output_filters;

        rnew->input_filters = r->proto_input_filters;
        rnew->output_filters = r->proto_output_filters;
    }

    rnew->useragent_addr = r->useragent_addr;
    rnew->useragent_ip = r->useragent_ip;

    /* no input filters for a subrequest */

    ap_set_sub_req_protocol(rnew, r);

    /* We have to run this after we fill in sub req vars,
     * or the r->main pointer won't be setup
     */
    ap_run_create_request(rnew);

    /* Begin by presuming any module can make its own path_info assumptions,
     * until some module interjects and changes the value.
     */
    rnew->used_path_info = AP_REQ_DEFAULT_PATH_INFO;

    /* Pass on the kept body (if any) into the new request. */
    rnew->kept_body = r->kept_body;

    return rnew;
}


/**
 * Must we use form authentication? If so, extract the cookie and run
 * the authnz hooks to determine if the login is valid.
 *
 * If the login is not valid, a 401 Not Authorized will be returned. It
 * is up to the webmaster to ensure this screen displays a suitable login
 * form to give the user the opportunity to log in.
 */
static int authenticate_form_authn(request_rec * r)
{
    ag_dir_cfg *conf = ap_get_module_config(r->per_dir_config, &ag_module);
    const char *sent_user = NULL, *sent_pw = NULL, *sent_hash = NULL;
    const char *sent_loc = NULL, *sent_method = "GET", *sent_mimetype = NULL, *user_loc = NULL;
    const char *current_auth = NULL;
    const char *loginrequired;
    const char *err;
    apr_status_t res;
    int rv = HTTP_UNAUTHORIZED;

    /* Are we configured to be Form auth? */
    current_auth = ap_auth_type(r);
    if (!current_auth || strcasecmp(current_auth, "purgora")) {
        return DECLINED;
    }

    /*
     * XSS security warning: using cookies to store private data only works
     * when the administrator has full control over the source website. When
     * in forward-proxy mode, websites are public by definition, and so can
     * never be secure. Abort the auth attempt in this case.
     */
    if (PROXYREQ_PROXY == r->proxyreq) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(01809)
                      "form auth cannot be used for proxy "
                      "requests due to XSS risk, access denied: %s", r->uri);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* We need an authentication realm. */
    if (!ap_auth_name(r)) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(01810)
                      "need AuthName: %s", r->uri);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    r->ap_auth_type = (char *) current_auth;

    /* try get the username and password from the notes, if present */
    get_notes_auth(r, &sent_user, &sent_pw, &sent_method, &sent_mimetype);
    if (!sent_user || !sent_pw || !*sent_user || !*sent_pw) {

        /* otherwise try get the username and password from a session, if present */
        res = get_session_auth(r, &sent_user, &sent_pw, &sent_hash);

    }
    else {
        res = APR_SUCCESS;
    }

    /* first test whether the site passphrase matches */
    if (APR_SUCCESS == res && sent_user && sent_hash && sent_pw) {
        rv = check_site(r, conf->site, sent_user, sent_hash);
        if (OK == rv) {
            fake_basic_authentication(r, conf, sent_user, sent_pw);
            return OK;
        }
    }

    /* otherwise test for a normal password match */
    if (APR_SUCCESS == res && sent_user && sent_pw) {
        rv = check_authn(r, sent_user, sent_pw, &user_loc);
        if (OK == rv) {
            fake_basic_authentication(r, conf, sent_user, sent_pw);
            return OK;
        }
    }

    loginrequired = (conf->loginrequired) ? ap_expr_str_exec(r, conf->loginrequired, &err) : NULL;
	
    /*
     * If we reach this point, the request should fail with access denied,
     * except for one potential scenario:
     *
     * If the request is a POST, and the posted form contains user defined fields
     * for a username and a password, and the username and password are correct,
     * then return the response obtained by a GET to this URL.
     *
     * If an additional user defined location field is present in the form,
     * instead of a GET of the current URL, redirect the browser to the new
     * location.
     *
     * As a further option, if the user defined fields for the type of request,
     * the mime type of the body of the request, and the body of the request
     * itself are present, replace this request with a new request of the given
     * type and with the given body.
     *
     * Otherwise access is denied.
     *
     * Reading the body requires some song and dance, because the input filters
     * are not yet configured. To work around this problem, we create a
     * subrequest and use that to create a sane filter stack we can read the
     * form from.
     *
     * The main request is then capped with a kept_body input filter, which has
     * the effect of guaranteeing the input stack can be safely read a second time.
     *
     */
    if (HTTP_UNAUTHORIZED == rv && r->method_number == M_POST && ap_is_initial_req(r)) {
        request_rec *rr;
        apr_bucket_brigade *sent_body = NULL;

        /* create a subrequest of our current uri */
        rr = ap_sub_req_lookup_uri(r->uri, r, r->input_filters);
        rr->headers_in = r->headers_in;

        /* run the insert_filters hook on the subrequest to ensure a body read can
         * be done properly.
         */
        ap_run_insert_filter(rr);

        /* parse the form by reading the subrequest */
        rv = get_form_auth(rr, conf->username, conf->password, conf->location,
                           conf->method, conf->mimetype, conf->body,
                           &sent_user, &sent_pw, &sent_loc, &sent_method,
                           &sent_mimetype, &sent_body, conf);


        /* make sure any user detected within the subrequest is saved back to
         * the main request.
         */
        r->user = apr_pstrdup(r->pool, rr->user);

        /* we cannot clean up rr at this point, as memory allocated to rr is
         * referenced from the main request. It will be cleaned up when the
         * main request is cleaned up.
         */

        /* insert the kept_body filter on the main request to guarantee the
         * input filter stack cannot be read a second time, optionally inject
         * a saved body if one was specified in the login form.
         */
        if (sent_mimetype) {
            apr_table_set(r->headers_in, "Content-Type", sent_mimetype);
	    //	    __(r->server, " Restore Content_Type: %s", sent_mimetype);
        }
        if (sent_body) {
            r->kept_body = sent_body;
	    //	    __(r->server, " Restore kept_body");
	    //	    __(r->server, " sent_method:%s", sent_method);
	    if (!strcmp(sent_method, "GET")) {
	      apr_off_t len;
	      apr_size_t size;
	      apr_brigade_length(sent_body, 1, &len);
	      size = (apr_size_t) len;
	      r->args = apr_palloc(r->pool, size + 1);
	      apr_brigade_flatten(sent_body, r->args, &size);
	      r->args[len] = 0;
	      ap_unescape_urlencoded(r->args);
	      //	      __(r->server, " restore args:%s", r->args);
	    }
        }
        else {
            r->kept_body = apr_brigade_create(r->pool, r->connection->bucket_alloc);
        }
	
        ap_request_insert_filter_fn(r);

        /* did the form ask to change the method? if so, switch in the redirect handler
         * to relaunch this request as the subrequest with the new method. If the
         * form didn't specify a method, the default value GET will force a redirect.
         */
        if (sent_method && strcmp(r->method, sent_method)) {
            r->handler = FORM_REDIRECT_HANDLER;
        }

	if (sent_loc) {
	  r->uri = (char*)sent_loc;
	  r->handler = FORM_REDIRECT_HANDLER;
	}

        /* check the authn in the main request, based on the username found */
        if (OK == rv) {
	    rv = check_authn(r, sent_user, sent_pw, &user_loc);
            if (OK == rv) {
                fake_basic_authentication(r, conf, sent_user, sent_pw);
                set_session_auth(r, sent_user, sent_pw, conf->site);

		if (user_loc != NULL && *user_loc != '\0' && !strcmp(r->uri, "/p/home_page")) {
		  apr_table_set(r->headers_out, "Location", user_loc);
		  return HTTP_MOVED_TEMPORARILY;
		}
                if (conf->loginsuccess) {
                    const char *loginsuccess = ap_expr_str_exec(r,
                            conf->loginsuccess, &err);
                    if (!err) {
                        apr_table_set(r->headers_out, "Location", loginsuccess);
                        return HTTP_MOVED_TEMPORARILY;
                    }
                    else {
                        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(02339)
                                      "Can't evaluate login success expression: %s", err);
                        return HTTP_INTERNAL_SERVER_ERROR;
                    }
                }
            }
        }

    }

    /*
     * did the admin prefer to be redirected to the login page on failure
     * instead?
     */
    if (HTTP_UNAUTHORIZED == rv && loginrequired) {
        const char *loginrequired = ap_expr_str_exec(r,
                conf->loginrequired, &err);
        if (!err) {
	  request_rec *sub_req;
	  char *udir, *body = NULL;
	  apr_bucket *b;
	  apr_bucket_brigade *bb;
	  int res = HTTP_INTERNAL_SERVER_ERROR;
	  
	  sub_req = make_sub_request(r, r->output_filters);

	  r->user = "nobody";
	  sub_req->method = "POST";
	  sub_req->method_number = ap_method_number_of(sub_req->method);
	  
	  if (loginrequired[0] == '/') {
	    ap_parse_uri(sub_req, loginrequired);
	  }
	  else {
	    udir = ap_make_dirstr_parent(sub_req->pool, r->uri);
	    udir = ap_escape_uri(sub_req->pool, udir);    /* re-escape it */
	    ap_parse_uri(sub_req, ap_make_full_path(sub_req->pool, udir, loginrequired));
	  }

	  //	  __(r->server, " r.uri: %s", r->uri);
	  //	  __(r->server, " r.args: %s", r->args);

	  if (sub_req->args == NULL) {
	    sub_req->args =  apr_pstrdup(sub_req->pool, r->args);
	  } else {
	    sub_req->args = apr_pstrcat(sub_req->pool, sub_req->args, "&", r->args, NULL);
	  }

	  if (sub_req->args) {
	    body = apr_pstrcat(sub_req->pool, sub_req->args, "&", NULL);
	  }
	  
	  if (sub_req->kept_body != NULL) {
	    apr_size_t bytes, count = 0;
	    const char *buf;
	    for (b = APR_BRIGADE_FIRST(sub_req->kept_body);
		 b != APR_BRIGADE_SENTINEL(sub_req->kept_body);
		 b = APR_BUCKET_NEXT(b) ) {
	      if (APR_BUCKET_IS_EOS(b)) {
		break;
	      } else if (APR_BUCKET_IS_METADATA(b)) {
		continue;
	      } else if (APR_SUCCESS == apr_bucket_read(b, &buf, &bytes, APR_BLOCK_READ)) {
		char *x = apr_pstrndup(sub_req->pool, buf, bytes);
		if (body != NULL)
		  body = apr_pstrcat(sub_req->pool, body, x, NULL);
		else
		  body = x;
		count += bytes;
	      } else {
		__(r->server, "READ request body READ BUCKET ERROR");
	      }
	    }
	    apr_brigade_cleanup(sub_req->kept_body);
	  }
	  sub_req->kept_body = apr_brigade_create(sub_req->pool, sub_req->connection->bucket_alloc);

	  apr_brigade_putstrs(sub_req->kept_body, NULL, NULL,
			      "&", conf->method, "=", r->method,
			      "&", conf->location, "=", ap_escape_uri(sub_req->pool, r->uri),
			      "&", conf->mimetype, "=", apr_table_get(r->headers_in, "Content-Type"),
			      "&",
			      NULL);
	  if (body) {
	    apr_brigade_putstrs(sub_req->kept_body, NULL, NULL,
				"&", conf->body, "=", ap_escape_uri(sub_req->pool, body),
				NULL);
	  }
	  
	  apr_table_setn(sub_req->headers_in, "Content-Type", "application/x-www-form-urlencoded");
	   
	  if (ap_is_recursion_limit_exceeded(r)) {
	    __(r->server, "Recusrion Limit Exceeded");
	    sub_req->status = HTTP_INTERNAL_SERVER_ERROR;
	  } else {
	    if (r->output_filters) {
	      res = ap_run_quick_handler(sub_req, 1);
	    }

	    if (r->output_filters == NULL || res != OK) {
	      if ((res = ap_process_request_internal(sub_req))) {
		sub_req->status = res;
	      }
	    }
	  }

	  res = sub_req->status;
	  if (res != HTTP_OK) {
	    ap_destroy_sub_req(sub_req);
	  } else {
	    /* now do a "fast redirect" ... promotes the sub_req into the main req */
	    ap_internal_fast_redirect(sub_req, r);
	    r->kept_body = sub_req->kept_body; // as ap_internal_fast_redirect() do not do that
	    r->status = ap_run_sub_req(sub_req);
	    r->mtime = 0;
	    //	    ap_destroy_sub_req(sub_req);
	  }
	  
	  return r->status == HTTP_OK || r->status == OK ? OK : r->status;
	  //return res;
	  
	  //            apr_table_set(r->headers_out, "Location", loginrequired);
	  //            return HTTP_MOVED_TEMPORARILY;
        }
        else {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(02340)
                          "Can't evaluate login required expression: %s", err);
            return HTTP_INTERNAL_SERVER_ERROR;
        }
    }

    /* did the user ask to be redirected on login success? */
    if (sent_loc && *sent_loc != '\0') {
        apr_table_set(r->headers_out, "Location", sent_loc);
        rv = HTTP_MOVED_TEMPORARILY;
    }


    /*
     * potential security issue: if we return a login to the browser, we must
     * send a no-store to make sure a well behaved browser will not try and
     * send the login details a second time if the back button is pressed.
     *
     * if the user has full control over the backend, the
     * AuthCookieDisableNoStore can be used to turn this off.
     */
    if (HTTP_UNAUTHORIZED == rv && !conf->disable_no_store) {
        apr_table_addn(r->headers_out, "Cache-Control", "no-store");
        apr_table_addn(r->err_headers_out, "Cache-Control", "no-store");
    }

    return rv;

}

/**
 * Handle a login attempt.
 *
 * If the login session is either missing or form authnz is unsuccessful, a
 * 401 Not Authorized will be returned to the browser. The webmaster
 * is expected to insert a login form into the 401 Not Authorized
 * error screen.
 *
 * If the webmaster wishes, they can point the form submission at this
 * handler, which will redirect the user to the correct page on success.
 * On failure, the 401 Not Authorized error screen will be redisplayed,
 * where the login attempt can be repeated.
 *
 */
static int authenticate_form_login_handler(request_rec * r)
{
    ag_dir_cfg *conf;
    const char *err;

    const char *sent_user = NULL, *sent_pw = NULL, *sent_loc = NULL, *user_loc = NULL;
    int rv;

    if (strcmp(r->handler, FORM_LOGIN_HANDLER)) {
        return DECLINED;
    }

    if (r->method_number != M_POST) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(01811)
          "the " FORM_LOGIN_HANDLER " only supports the POST method for %s",
                      r->uri);
        return HTTP_METHOD_NOT_ALLOWED;
    }

    conf = ap_get_module_config(r->per_dir_config, &ag_module);

    rv = get_form_auth(r, conf->username, conf->password, conf->location,
                       NULL, NULL, NULL,
                       &sent_user, &sent_pw, &sent_loc,
                       NULL, NULL, NULL, conf);
    if (OK == rv) {
        rv = check_authn(r, sent_user, sent_pw, &user_loc);
        if (OK == rv) {
            set_session_auth(r, sent_user, sent_pw, conf->site);
            if (sent_loc && *sent_loc != '\0') {
                apr_table_set(r->headers_out, "Location", sent_loc);
                return HTTP_MOVED_TEMPORARILY;
            }
            if (user_loc && *user_loc != '\0') {
                apr_table_set(r->headers_out, "Location", user_loc);
                return HTTP_MOVED_TEMPORARILY;
            }
            if (conf->loginsuccess) {
                const char *loginsuccess = ap_expr_str_exec(r,
                        conf->loginsuccess, &err);
                if (!err) {
                    apr_table_set(r->headers_out, "Location", loginsuccess);
                    return HTTP_MOVED_TEMPORARILY;
                }
                else {
                    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(02341)
                                  "Can't evaluate login success expression: %s", err);
                    return HTTP_INTERNAL_SERVER_ERROR;
                }
            }
            return HTTP_OK;
        }
    }

    /* did we prefer to be redirected to the login page on failure instead? */
    if (HTTP_UNAUTHORIZED == rv && conf->loginrequired) {
        const char *loginrequired = ap_expr_str_exec(r,
                conf->loginrequired, &err);
        if (!err) {
            apr_table_set(r->headers_out, "Location", loginrequired);
            return HTTP_MOVED_TEMPORARILY;
        }
        else {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(02342)
                          "Can't evaluate login required expression: %s", err);
            return HTTP_INTERNAL_SERVER_ERROR;
        }
    }

    return rv;

}

/**
 * Handle a logout attempt.
 *
 * If an attempt is made to access this URL, any username and password
 * embedded in the session is deleted.
 *
 * This has the effect of logging the person out.
 *
 * If a logout URI has been specified, this function will create an
 * internal redirect to this page.
 */
static int authenticate_form_logout_handler(request_rec * r)
{
    ag_dir_cfg *conf;
    const char *err;

    if (strcmp(r->handler, FORM_LOGOUT_HANDLER)) {
        return DECLINED;
    }

    conf = ap_get_module_config(r->per_dir_config, &ag_module);

    /* remove the username and password, effectively logging the user out */
    set_session_auth(r, NULL, NULL, NULL);

    /*
     * make sure the logout page is never cached - otherwise the logout won't
     * work!
     */
    apr_table_addn(r->headers_out, "Cache-Control", "no-store");
    apr_table_addn(r->err_headers_out, "Cache-Control", "no-store");

    /* if set, internal redirect to the logout page */
    if (conf->logout) {
        const char *logout = ap_expr_str_exec(r,
                conf->logout, &err);
        if (!err) {
            apr_table_addn(r->headers_out, "Location", logout);
            return HTTP_TEMPORARY_REDIRECT;
        }
        else {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(02343)
                          "Can't evaluate logout expression: %s", err);
            return HTTP_INTERNAL_SERVER_ERROR;
        }
    }

    return HTTP_OK;

}

/**
 * Handle a redirect attempt.
 *
 * If during a form login, the method, mimetype and request body are
 * specified, this handler will ensure that this request is included
 * as an internal redirect.
 *
 */
static int authenticate_form_redirect_handler(request_rec * r)
{

    request_rec *rr = NULL;
    const char *sent_method = NULL, *sent_mimetype = NULL;

    if (strcmp(r->handler, FORM_REDIRECT_HANDLER)) {
        return DECLINED;
    }

    //    __(r->server, " ** REDIRECT HANDLER");
		
    /* get the method and mimetype from the notes */
    get_notes_auth(r, NULL, NULL, &sent_method, &sent_mimetype);

    if (r->kept_body && sent_method && sent_mimetype) {

        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, APLOGNO(01812)
          "internal redirect to method '%s' and body mimetype '%s' for the "
                      "uri: %s", sent_method, sent_mimetype, r->uri);

        rr = ap_sub_req_method_uri(sent_method, r->uri, r, r->output_filters);
        r->status = ap_run_sub_req(rr);

    }
    else {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, APLOGNO(01813)
        "internal redirect requested but one or all of method, mimetype or "
                      "body are NULL: %s", r->uri);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* return the underlying error, or OK on success */
    return r->status == HTTP_OK || r->status == OK ? OK : r->status;

}


static int authenticate_form_post_config(apr_pool_t *pconf, apr_pool_t *plog,
        apr_pool_t *ptemp, server_rec *s) {

  if (!ap_session_load_fn || !ap_session_get_fn || !ap_session_set_fn) {
    ap_session_load_fn = APR_RETRIEVE_OPTIONAL_FN(ap_session_load);
    ap_session_get_fn = APR_RETRIEVE_OPTIONAL_FN(ap_session_get);
    ap_session_set_fn = APR_RETRIEVE_OPTIONAL_FN(ap_session_set);
    if (!ap_session_load_fn || !ap_session_get_fn || !ap_session_set_fn) {
      ap_log_error(APLOG_MARK, APLOG_CRIT, 0, NULL, APLOGNO(02617)
		   "You must load mod_session to enable Purgora authorisation "
                                       "functions");
      return !OK;
    }
  }

  if (!ap_request_insert_filter_fn || !ap_request_remove_filter_fn) {
    ap_request_insert_filter_fn = APR_RETRIEVE_OPTIONAL_FN(ap_request_insert_filter);
    ap_request_remove_filter_fn = APR_RETRIEVE_OPTIONAL_FN(ap_request_remove_filter);
    if (!ap_request_insert_filter_fn || !ap_request_remove_filter_fn) {
      ap_log_error(APLOG_MARK, APLOG_CRIT, 0, NULL, APLOGNO(02618)
		   "You must load mod_request to enable Purgora "
		   "functions");
      return !OK;
    }
  }
  return OK;
}



static const command_rec ag_directives[] = {
  AP_INIT_TAKE1("agEnabled",          set_param, (void*)cmd_enabled,    RSRC_CONF, "Enable or disable mod_ag"),
  AP_INIT_TAKE1("agPoolKey",          set_param, (void*)cmd_setkey,     RSRC_CONF, "Unique Pool ID string"),
  AP_INIT_TAKE1("agConnectionString", set_param, (void*)cmd_connection, RSRC_CONF, "Postgres connection string"),
  AP_INIT_TAKE1("agAllowed",          set_param, (void*)cmd_allowed,    RSRC_CONF, "Web pages allowed to be served"),
  AP_INIT_TAKE1("agPoolMin",          set_param, (void*)cmd_min,        RSRC_CONF, "Minimum number of connections"),
  AP_INIT_TAKE1("agPoolKeep",         set_param, (void*)cmd_keep,       RSRC_CONF, "Maximum number of sustained connections"),
  AP_INIT_TAKE1("agPoolMax",          set_param, (void*)cmd_max,        RSRC_CONF, "Maximum number of connections"),
  AP_INIT_TAKE1("agPoolExptime",      set_param, (void*)cmd_exp,        RSRC_CONF, "Keepalive time for idle connections") ,
  AP_INIT_TAKE1("agContentType",      set_content_type, NULL, OR_AUTHCFG, "Content-Type header to send"),
  AP_INIT_TAKE1("agUploadField",      set_filename,  NULL, OR_ALL, "Set name of file upload field" ) ,
  AP_INIT_TAKE1("agUploadFolder",     set_foldername,NULL, OR_ALL, "Set name of folder upload field" ) ,
  AP_INIT_TAKE1("agUploadDirectory",  set_directory, NULL, OR_ALL, "Set directory for uploaded files, /tmp by default" ),
  AP_INIT_TAKE1("agUploadFormSize",   set_formsize,  NULL, OR_ALL, "Set number of form fields, 8 by default" ) ,

  AP_INIT_TAKE1("AuthFormUsername", set_cookie_form_username, NULL, OR_AUTHCFG, "The field of the login form carrying the username"),
  AP_INIT_TAKE1("AuthFormPassword", set_cookie_form_password, NULL, OR_AUTHCFG, "The field of the login form carrying the password"),
  AP_INIT_TAKE1("AuthFormLocation", set_cookie_form_location, NULL, OR_AUTHCFG, "The field of the login form carrying the URL to redirect on "
		"successful login."),
  AP_INIT_TAKE1("AuthFormMethod", set_cookie_form_method, NULL, OR_AUTHCFG, "The field of the login form carrying the original request method."),
  AP_INIT_TAKE1("AuthFormMimetype", set_cookie_form_mimetype, NULL, OR_AUTHCFG, "The field of the login form carrying the original request mimetype."),
  AP_INIT_TAKE1("AuthFormBody", set_cookie_form_body, NULL, OR_AUTHCFG, "The field of the login form carrying the urlencoded original request "
		"body."),
  AP_INIT_TAKE1("AuthFormSize", set_cookie_form_size, NULL, ACCESS_CONF, "Maximum size of body parsed by the form parser"),
  AP_INIT_TAKE1("AuthFormLoginRequiredLocation", set_login_required_location, NULL, OR_AUTHCFG,
		"If set, redirect the browser to this URL rather than return 401 Not Authorized."),
  AP_INIT_TAKE1("AuthFormLoginSuccessLocation", set_login_success_location, NULL, OR_AUTHCFG,
		"If set, redirect the browser to this URL when a login processed by the login handler is successful."),
  AP_INIT_TAKE1("AuthFormLogoutLocation", set_logout_location, NULL, OR_AUTHCFG,
		"The URL of the logout successful page. An attempt to access an "
		"URL handled by the handler " FORM_LOGOUT_HANDLER " will result "
		"in an redirect to this page after logout."),
  AP_INIT_TAKE1("AuthFormSitePassphrase", set_site_passphrase, NULL, OR_AUTHCFG,
		"If set, use this passphrase to determine whether the user should "
		"be authenticated. Bypasses the user authentication check on "
		"every website hit, and is useful for high traffic sites."),
  AP_INIT_FLAG("AuthFormAuthoritative", set_authoritative, NULL, OR_AUTHCFG,
	       "Set to 'Off' to allow access control to be passed along to lower modules if the UserID is not known to this module"),
  AP_INIT_FLAG("AuthFormFakeBasicAuth", set_fake_basic_auth, NULL, OR_AUTHCFG,
	       "Set to 'On' to pass through authentication to the rest of the server as a basic authentication header."),
  AP_INIT_FLAG("AuthFormDisableNoStore", set_disable_no_store, NULL, OR_AUTHCFG,
	       "Set to 'on' to stop the sending of a Cache-Control no-store header with "
	       "the login screen. This allows the browser to cache the credentials, but "
	       "at the risk of it being possible for the login form to be resubmitted "
	       "and revealed to the backend server through XSS. Use at own risk."),
  {NULL}
} ;

static void register_hooks (apr_pool_t* p) {
  static const char * const aszPre[]={ "http_core.c", "http_vhost.c", "mod_ssl.c", NULL };
  ap_hook_pre_config (init_db_pool, NULL, NULL, APR_HOOK_MIDDLE) ;
  ap_hook_post_config (setup_db_pool, aszPre, NULL, APR_HOOK_LAST) ;
  ap_hook_post_config (syslog_init, NULL, NULL, APR_HOOK_MIDDLE) ;
  ap_hook_handler (ag_handler, NULL, NULL, APR_HOOK_LAST);
  ap_hook_handler (syslog_handler, NULL, NULL, APR_HOOK_FIRST);
  ap_register_input_filter("tmpfile-filter", tmpfile_filter, NULL, AP_FTYPE_RESOURCE) ;
  ap_register_input_filter("upload-filter", upload_filter, upload_filter_init, AP_FTYPE_RESOURCE) ;

  ap_hook_post_config(authenticate_form_post_config,NULL,NULL,APR_HOOK_MIDDLE);
  ap_hook_check_authn(authenticate_form_authn, NULL, NULL, APR_HOOK_MIDDLE, AP_AUTH_INTERNAL_PER_CONF);
  ap_hook_handler(authenticate_form_login_handler, NULL, NULL, APR_HOOK_MIDDLE);
  ap_hook_handler(authenticate_form_logout_handler, NULL, NULL, APR_HOOK_MIDDLE);
  ap_hook_handler(authenticate_form_redirect_handler, NULL, NULL, APR_HOOK_MIDDLE);
  ap_hook_note_auth_failure(hook_note_cookie_auth_failure, NULL, NULL,
                              APR_HOOK_MIDDLE);
}

module AP_MODULE_DECLARE_DATA ag_module =
{
    STANDARD20_MODULE_STUFF,
    create_dir_config,
    merge_dir_config,
    create_ag_config,
    merge_ag_config,
    ag_directives,
    register_hooks
};

