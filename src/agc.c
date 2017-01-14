/* appGoo Compiler
 *
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <regex.h>

#include "template_pgsql_function_begin_sql.h"
#include "template_pgsql_function_end_sql.h"

#define true 1
#define false 0

#define MAX_INPUT_CHARS 4090
#define MAX_STRING_CHARS 2000
#define NSUBS 6

int       in_code = false, in_equals = false, in_declare = false, in_header = true, in_params = false;
int       in_sql_comment = false, in_html_comment = false, to_skip = false;
int       tag_processed = false, is_first_line = true, is_including = false;
FILE *    f;
FILE *    f_source;
char      line_input [MAX_INPUT_CHARS + 4];
char *    line_trimmed;
char      environment = 'd'; /* d = dev, t = test, p = prod */
int       i, j;

char      function_name  [MAX_STRING_CHARS];
char      py_line        [2*MAX_STRING_CHARS];
char      return_type    [MAX_STRING_CHARS] = {"text"};
char      content_type    [MAX_STRING_CHARS] = {"text/html"};


regex_t preg;


void expand_psp_calls(char *line) {
  int rc;
  regmatch_t	subs[NSUBS];
  char      new_line[MAX_STRING_CHARS];
  while (0 == (rc = regexec(&preg, line, NSUBS, subs, 0))) {
    snprintf(new_line, sizeof(new_line),
	     "%.*spsp_%.*s(%.*s,%.*s, _e_, _mode_)%.*s",
	     subs[1].rm_eo - subs[1].rm_so,
	     line + subs[1].rm_so,
	     subs[2].rm_eo - subs[2].rm_so,
	     line + subs[2].rm_so,
	     subs[3].rm_eo - subs[3].rm_so,
	     line + subs[3].rm_so,
	     subs[4].rm_eo - subs[4].rm_so,
	     line + subs[4].rm_so,
	     subs[5].rm_eo - subs[5].rm_so,
	     line + subs[5].rm_so
	     );
    strncpy(line, new_line, sizeof(new_line));
  }
}



int main(int argc, char * argv[])
{
  int rc;

  if (argc == 1) { fprintf(stderr, "Usage: agc input_file.ag [-d|-t|-p]\n\n"); exit(EXIT_FAILURE); }

  if (argc == 3 && ( argv[2][1] == 't' || argv[2][1] == 'p') ) { environment = argv[2][1]; }

  fprintf(stderr, "Compiling \"%s\" to environment \"%c\"\n", argv[1], environment);


  if (0 != (rc = regcomp(&preg, "^(.*)psp_([^\\(]*)\\(([^\\,]*)\\,([^\\,\\)]*)\\)(.*)$", REG_EXTENDED))) {
    fprintf(stderr, "error compiling regex: %d - %d (%s)\n", rc, errno, strerror(errno));
    exit(EXIT_FAILURE);
  }
  

  if (strstr(argv[1], ".include.")) { fprintf(stderr, "Warning: skipping an include file\n\n"); exit(EXIT_SUCCESS); }

  f_source = fopen (argv[1], "r");

  while ( (!is_including && fgets (line_input, MAX_INPUT_CHARS, f_source) != NULL) || is_including )
    {
      if (is_including)
        {
	  if (fgets (line_input, MAX_STRING_CHARS, f) == NULL)
            {
	      fclose(f);
	      is_including = false;
	      continue;
            }
        }
      else
        {
	  if (line_input[0] == '#' && strcasestr(line_input, "#include"))
            {

	      i = 0;
	      while (line_input[i])
		{
		  /* removing new-line characters and tabs */
		  if (line_input[i] == 0x0d || line_input[i] == 0x0a) line_input[i] = 0x00;
		  if (line_input[i] == '\t') line_input[i] = ' ';
		  i++;
		}


	      fprintf(stderr, "Including file \"%s\"\n", line_input+9);
	      f = fopen (line_input+9, "r");
	      if (f == NULL) {
		fprintf(stderr, "error opening file %s -- %d (%s)\n", line_input + 9, errno, strerror(errno));
		exit(EXIT_FAILURE);
	      }
	      is_including = true;
	      continue;
            }
        }

      i = 0;
      while (line_input[i])
	{
	  /* removing new-line characters and tabs */
	  if (line_input[i] == 0x0d || line_input[i] == 0x0a) line_input[i] = 0x00;
	  if (line_input[i] == '\t') line_input[i] = ' ';
	  i++;
	}

      /* adding extra end-of-string zeroes to avoid out-of-bound comparisons */
      line_input[i+1] = line_input[i+2] = line_input[i+3] = 0x00;

      line_trimmed = line_input;
      while (line_trimmed[0] == ' ') line_trimmed++;

      if (line_trimmed[0] == '#' && NULL != strcasestr(line_trimmed + 1, "return-type")) {
	char *p = strchr(line_trimmed + 12, (int)' ');
	if (p == NULL) p = strchr(line_trimmed + 12, (int)':');
	if (p == NULL) {
	  fprintf(stderr, "Can not get return type in command: %s\n", line_trimmed);
	  exit(EXIT_FAILURE);
	}
	while(*p == ' ' && p[1] == ' ') p++;
	strncpy(return_type, p + 1, sizeof(return_type));
	continue;
      }
      
      if (line_trimmed[0] == '#' && NULL != strcasestr(line_trimmed + 1, "content-type")) {
	char *p = strchr(line_trimmed + 13, (int)' ');
	if (p == NULL) p = strchr(line_trimmed + 13, (int)':');
	if (p == NULL) {
	  fprintf(stderr, "Can not get content type in command: %s\n", line_trimmed);
	  exit(EXIT_FAILURE);
	}
	while(*p == ' ' && p[1] == ' ') p++;
	strncpy(content_type, p + 1, sizeof(content_type));
 	continue;
     }
      
      if (line_trimmed[0] == '#' && NULL != strcasestr(line_trimmed + 1, "environment")) {
	char *p = strchr(line_trimmed + 13, (int)' ');
	if (p == NULL) p = strrchr(line_trimmed + 13, (int)':');
	if (p == NULL) {
	  fprintf(stderr, "Can not get environment in command: %s\n", line_trimmed);
	  exit(EXIT_FAILURE);
	}
	while(*p == ' ' && p[1] == ' ') p++;
	environment = tolower((int)p[1]);
	continue;
      }
      
      /* comments section in the beginning of .ag file, only works until first non-comment line */
      if (line_trimmed[0] == '#' && is_first_line) line_trimmed[0] = 0;

      if (line_trimmed[0] == 0 || (line_trimmed[0] == '/' && line_trimmed[1] == '/')) continue;

      if (is_first_line)
	{
	  int encounter = 0;
	  sprintf(function_name, "%s", line_trimmed);

	  /* importing function start */
	  f = fmemopen ( src_template_pgsql_function_begin_sql,
			 (size_t)src_template_pgsql_function_begin_sql_len,
			 "r");
	  while (fgets(py_line, MAX_STRING_CHARS, f) != NULL)
	    if ( strstr(py_line, "%s") ) {
	      switch(++encounter) {
	      case 3:
		printf(py_line, return_type); break;
	      default:
		printf(py_line, function_name); break;
	      }
	    } else puts(py_line);
	  fclose(f);

	  is_first_line = false;
	  in_params = true;
	}
      else
	{
	  if (line_trimmed[0] == '#' && line_trimmed[1] == 'l' && line_trimmed[2] == 'o' && line_trimmed[3] == 'g')
	    {
	      if (environment == 'd') printf ("console.log(%s);\n", line_trimmed+5);
	      line_trimmed[0] = 0;
	    }

	  /* variables declaration tag <! !> */
	  if (line_trimmed[0] == '<' && (line_trimmed[1] == '!' || line_trimmed[1] == '?') && line_trimmed[2] != '-')
	    {
	      in_header = false; /* as soon as we reach the declare section, the header section stops */
	      in_params = false;
	      in_declare = true;
	      line_trimmed += 2;
	    }

	  if ( (line_trimmed[0] == '!' || line_trimmed[0] == '?') && line_trimmed[1] == '>')
	    {
	      in_declare = false;
	      printf("begin\n");
	      if (environment == 'p') printf("_e_ := 0;\n");
	      printf("if _e_ > 0 then _p_[_n_] := '<!-- start: %s -->'; _n_ := _n_ + 1; end if;\n", function_name);
	      printf("_p_[_n_] := \'");
	      line_trimmed += 2;
	    }

	  /* processing parameters in HTTP GET passed by mod_apache */
	  if (in_params)
	    {
	      int not_null;
	      i = 0;

	      /* finding first white space */
	      while (line_trimmed[i] != ' ' && line_trimmed[i] != '\t') i++;
	      j = i;
	      while (line_trimmed[i] == ' ' || line_trimmed[i] == '\t') i++;

	      /* finding second white space */
	      while (line_trimmed[i] != ' ' && line_trimmed[i] != '\t' && line_trimmed[i] != 0x00) i++;
	      while (line_trimmed[i] == ' ' || line_trimmed[i] == '\t') i++;

	      /* Input  : parameter type default
		 Parsed : parameter [j] type [i] default
		 Output : parameter type := ag_parse_get[_type] (_ag_GET_, 'parameter', 'default'); */

	      not_null = strcasecmp(line_trimmed+i, "null");

	      printf("%.*s:= ag_parse_get%s%s%s(_ag_GET_, \'%.*s\', %s%s%s);\n",
		     i, line_trimmed,
		     ( strstr(line_trimmed, " date ") ) ? "_date" : "",
		     ( strstr(line_trimmed, " json ") ) ? "_json" : "",
		     ( strstr(line_trimmed, " timestamptz ") ) ? "_timestamptz" : "",
		     j, line_trimmed,
		     (not_null) ? "'" : "",
		     line_trimmed+i,
		     (not_null) ? "'" : ""
		     );

	      continue;
	    }

	  expand_psp_calls(line_trimmed);
	  /* function name, passed parameters, and declared variables - done, now processing the rest */
	  i = 0;
	  while (line_trimmed[i])
	    {
	      do
		{
		  tag_processed = false; /* to cover 2+ consecutive tags */

		  /* checking for comments */
		  if ((in_code || in_equals) && line_trimmed[i] == '/' && line_trimmed[i+1] == '*')
		    {
		      in_sql_comment = true;
		      i += 2;
		    }

		  if ((in_code || in_equals) && in_sql_comment && line_trimmed[i] == '*' && line_trimmed[i+1] == '/')
		    {
		      in_sql_comment = false;
		      i += 2;
		    }

		  if (!in_code && !in_equals && line_trimmed[i] == '<' && line_trimmed[i+1] == '!' && line_trimmed[i+2] == '-' && line_trimmed[i+3] == '-' && line_trimmed[i+4] != '#')
		    {
		      in_html_comment = true;
		      i += 4;
		    }

		  if (!in_code && !in_equals && in_html_comment && line_trimmed[i] == '-' && line_trimmed[i+1] == '-' && line_trimmed[i+2] == '>')
		    {
		      in_html_comment = false;
		      i += 3;
		    }

		  /* code tag <% %> but not print tag <%= %> */
		  if (!in_code && line_trimmed[i] == '<' && line_trimmed[i+1] == '%' && line_trimmed[i+2] != '=')
		    {
		      in_code = true;
		      //printf("\';");
		      printf("\'; _n_ := _n_ + 1;");

		      i += 2;
		      tag_processed = true;
		    }

		  if (line_trimmed[i] == '%' && line_trimmed[i+1] == '>')
		    {
		      if (in_code)
			{
			  in_code = false;
			  printf("_p_[_n_] := \'");
			  i += 2;
			  tag_processed = true;
			}

		      if (in_equals)
			{
			  in_equals = false;
			  printf(") || \'");
			  i += 2;
			  tag_processed = true;
			}
		    }

		  /* classic style print tag <%= %> */
		  if (!in_code && line_trimmed[i] == '<' && line_trimmed[i+1] == '%' && line_trimmed[i+2] == '=')
		    {
		      in_equals = true;
		      printf("\' || (");

		      i += 3;
		      tag_processed = true;
		    }

		}
	      while (tag_processed);

	      /* removing comments, both pgsql and html */
	      to_skip = false;
	      if ( (  in_code ||  in_equals ) && in_sql_comment  ) to_skip = true;
	      if ( ( !in_code && !in_equals ) && in_html_comment ) to_skip = true;

	      if (i > 0 && line_trimmed[i] == ' ' && line_trimmed[i-1] == ' ') to_skip = true;

	      if (!to_skip)
		{
		  if (!in_code && !in_equals && !in_declare && !in_header && line_trimmed[i] == '\'') printf("%c", line_trimmed[i]);
		  if (line_trimmed[i]) printf("%c", line_trimmed[i]);
		}

	      i++;

	    } /* regular line */

	} /* not first line */

      printf("\n");

    } /* while fgets */

  printf("\';\n_n_ := _n_ + 1;\n");

  /* importing function end */
  f = fmemopen ( src_template_pgsql_function_end_sql,
		 (size_t)src_template_pgsql_function_end_sql_len,
		 "r");
  while (fgets(py_line, MAX_STRING_CHARS, f) != NULL) if ( strstr(py_line, "%s") ) printf(py_line, function_name); else puts(py_line);
  fclose(f);

  fclose (f_source);


  exit (EXIT_SUCCESS);

} /* main */

