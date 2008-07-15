#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include "old_parser.h"

enum {
	TOK_INT,
	TOK_FLOAT,
	TOK_STRING,
	TOK_EQ,
	TOK_SECTION_B,
	TOK_SECTION_E,
	TOK_ARRAY_B,
	TOK_ARRAY_E,
	TOK_IDENTIFIER,
	TOK_COMMA,
	TOK_EOF
};


typedef struct parser {
  const char *data;		/* data and data limits */
  const char *db, *de;

  int t;			/* token limits and type */
  const char *tb, *te;

  int line;			/* line we're on */
} parser_t;


static void _get_token(struct parser *p);
static void _eat_space(struct parser *p);
static ccs_node_t *_file(struct parser *p);
static ccs_node_t *_section(struct parser *p);
static ccs_value_t *_value(struct parser *p);
static ccs_value_t *_type(struct parser *p);
static int _match_aux(struct parser *p, int t);
static ccs_value_t *_create_value(void);
static ccs_node_t *_create_node(void);
static char *_dup_tok(struct parser *p);


#define match(t) do {\
   if (!_match_aux(p, (t))) {\
      fprintf(stderr, \
      	            "Parse error at line %d: unexpected token\n",  p->line); \
      return 0;\
   } \
} while(0);


static void free_ccs_value(ccs_value_t *cv){
  if(cv->next)
    free_ccs_value(cv->next);
  if(cv->type == CCS_STRING && cv->v.str)
    free(cv->v.str);
  free(cv);
}

void free_ccs_node(ccs_node_t *cn){
  ccs_node_t *tmp, *sib_end;

  for(tmp = cn; tmp->sib; tmp = tmp->sib);
  sib_end = tmp;
    
  for(tmp = cn; tmp; tmp = tmp->sib){
    if(tmp->child){
      sib_end->sib = tmp->child;
      tmp->child = NULL;
      while(sib_end->sib){
	sib_end = sib_end->sib;
      }
    }
  }

  while(cn){
    tmp = cn;
    cn = cn->sib;
    if(tmp->v){
      free_ccs_value(tmp->v);
    }
    if(tmp->key){
      free(tmp->key);
    }
    free(tmp);
  }
}

static int isidchar(char c){
  if ( (c >= 'a' && c <= 'z') || /* [a-z] */
       (c >= 'A' && c <= 'Z') || /* [A-Z] */
       (c >= '0' && c <= '9') || /* [0-9] */
       (c == '_' ) ||
       (c == '$' ) ||
       (c == '-' ) ||
       (c == '.' ) )
	  return 1;
  else
	  return 0;
}

static void _get_token(struct parser *p){
  int number_valid = 1;

  p->tb = p->te;
  _eat_space(p);
  if (p->tb == p->de){
    p->t = TOK_EOF;
    return;
  }
  
  p->t = TOK_INT;         /* fudge so the fall through for
			     floats works */
  switch (*p->te) {
  case '{':
    p->t = TOK_SECTION_B;
    p->te++;
    break;
    
  case '}':
    p->t = TOK_SECTION_E;
    p->te++;
    break;

  case '[':
    p->t = TOK_ARRAY_B;
    p->te++;
    break;

  case ']':
    p->t = TOK_ARRAY_E;
    p->te++;
    break;

  case ',':
    p->t = TOK_COMMA;
    p->te++;
    break;

  case '=':
    p->t = TOK_EQ;
    p->te++;
    break;

  case '"':
    p->t = TOK_STRING;
    p->te++;
    while ((p->te != p->de) && (*p->te != '"')) {
      /* $ is reserved */
      if (*p->te == '$') 
      {
	      /* set token to EOF due to parse error */
	      p->t = TOK_EOF;
	      break;
      }
      
      if ((*p->te == '\\') && (p->te + 1 != p->de))
	p->te++;
      p->te++;
    }
    
    if (p->te != p->de)
      p->te++;
    break;

  case '\'':
    p->t = TOK_STRING;
    p->te++;
    while ((p->te != p->de) && (*p->te != '\'')) {
      p->te++;
    }
    
    if (p->te != p->de)
      p->te++;
    break;

  case '.':
    p->t = TOK_FLOAT;
  case '-':
  case '+':
    number_valid = 0;
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    p->te++;
    while (p->te != p->de) {
      if (*p->te == '.') {
	if (p->t == TOK_FLOAT)
	  break;
	p->t = TOK_FLOAT;
      } else if (!isdigit((int) *p->te)){
	break;
      }
      number_valid = 1;
      p->te++;
    }
    if (!number_valid){
      p->t = TOK_EOF;
    } else if(!isidchar(*p->te)){
      break;
    }
  default:


    /* NOTE:  at this point, [-.0-9] have already been checked.  This means
     * that valid identifiers can begin with [$_a-zA-Z] */
    if (isidchar(*p->te))
    {
      p->t = TOK_IDENTIFIER;
      while ((p->te != p->de) && !isspace(*p->te) &&
	     (isidchar(*p->te)) && (*p->te != '#') && (*p->te != '='))
         p->te++;
    }else
      p->t = TOK_EOF;

    break;
  }
}


static void _eat_space(struct parser *p){
  while (p->tb != p->de) {
    if (*p->te == '#') {
      while ((p->te != p->de) && (*p->te != '\n'))
	p->te++;
      /*      p->line++;
	      since != \n, the \n will be picked up by else if */
    }

    else if (isspace(*p->te)) {
      while ((p->te != p->de) && isspace(*p->te)) {
	if (*p->te == '\n')
	  p->line++;
	p->te++;
      }
    }

    else
      return;

    p->tb = p->te;
  }
}


static ccs_node_t *_file(struct parser *p){
  ccs_node_t *root = NULL, *n, *l = NULL;

  while (p->t != TOK_EOF) {
    if (!(n = _section(p))) {
      return 0;
    }
    
    if (!root)
      root = n;
    else
      l->sib = n;
    l = n;
  }
  return root;
}


static ccs_node_t *_section(struct parser *p){
  /* IDENTIFIER '{' VALUE* '}' */
  ccs_node_t *root, *n, *l = NULL;

  if (!(root = _create_node())) {
    return 0;
  }

  if (!(root->key = _dup_tok(p))) {
    free_ccs_node(root);
    return 0;
  }

  match(TOK_IDENTIFIER);

  if (p->t == TOK_SECTION_B) {
    match(TOK_SECTION_B);
    while (p->t != TOK_SECTION_E) {
      if (!(n = _section(p))) {
	free_ccs_node(root);
	return 0;
      }

      if (!root->child)
	root->child = n;
      else
	l->sib = n;
      l = n;
    }
    match(TOK_SECTION_E);
  } else {
    match(TOK_EQ);
    if (!(root->v = _value(p))) {
      free_ccs_node(root);
      return 0;
    }
  }

  return root;
}


static ccs_value_t *_value(struct parser *p){
  /* '[' TYPE* ']' | TYPE */
  ccs_value_t *h = 0, *l, *ll = 0;
  if (p->t == TOK_ARRAY_B) {
    match(TOK_ARRAY_B);
    while (p->t != TOK_ARRAY_E) {
      if (!(l = _type(p))) {
	return 0;
      }
      
      if (!h)
	h = l;
      else
	ll->next = l;
      ll = l;
      
      if (p->t == TOK_COMMA)
	match(TOK_COMMA);
    }
    match(TOK_ARRAY_E);
  } else
    h = _type(p);
  
  return h;
}


static ccs_value_t *_type(struct parser *p){
  /* [0-9]+ | [0-9]*\.[0-9]* | ".*" */
  ccs_value_t *v;

  if(!(v = _create_value())){
    return NULL;
  }

  switch (p->t) {
  case TOK_INT:
    v->type = CCS_INT;
    v->v.i = strtol(p->tb, 0, 0);   /* FIXME: check error */
    match(TOK_INT);
    break;

#ifndef __KERNEL__
  case TOK_FLOAT:
    v->type = CCS_FLOAT;
    v->v.r = strtod(p->tb, 0);      /* FIXME: check error */
    match(TOK_FLOAT);
    break;
#endif

  case TOK_STRING:
    v->type = CCS_STRING;

    p->tb++, p->te--;       /* strip double-quotes */
    if (!(v->v.str = _dup_tok(p))) {
      free_ccs_value(v);
      return NULL;
    } else {
      char *tmp;
      /* strip \'s */
      while((tmp = strstr(v->v.str, "\\"))){
	strcpy(tmp, tmp+1);
      }
    }
    p->te++;
    match(TOK_STRING);
    break;

  default:
    fprintf(stderr, "Error on line %d\n", p->line);
    free_ccs_value(v);
    return NULL;
  }
  return v;
}


static int _match_aux(struct parser *p, int t){
  if (p->t != t){
    return 0;
  }
  _get_token(p);

  return 1;
}


static ccs_value_t *_create_value(void){
  ccs_value_t *v = (ccs_value_t *)malloc(sizeof(ccs_value_t));
  if(v){
    memset(v, 0, sizeof(ccs_value_t));
    return v;
  }
  return NULL;
}


static ccs_node_t *_create_node(void){
  ccs_node_t *n = (ccs_node_t *)malloc(sizeof(ccs_node_t));
  if(n){
    memset(n, 0, sizeof(ccs_node_t));
    return n;
  }
  return NULL;
}


static char *_dup_tok(struct parser *p){
  int len = p->te - p->tb;
  char *str = (char *)malloc(len + 1);
  if (!str) {
    return NULL;
  }
  strncpy(str, p->tb, len);
  str[len] = '\0';
  return str;
}


static int get_ccs_file(int fd, char **file_data, char *filename){
  int error = 0;
  int size;
  char *data=NULL;
  char *beginning, *end;
  char search_str[128];

  if((size = lseek(fd, 0, SEEK_END)) < 0){
    fprintf(stderr, "Unable to seek to end of archive: %s\n", strerror(errno));
    error = -errno;
    goto fail;
  }

  if(lseek(fd, 0, SEEK_SET) < 0){
    fprintf(stderr, "Bad lseek on archive: %s\n", strerror(errno));
    error = -errno;
    goto fail;
  }

  size++;
  data = malloc(size);
  memset(data, 0, size);
  size--;

  if(read(fd, data, size) != size){
    fprintf(stderr, "Unable to read entire archive.\n");
    error = -EIO;
    goto fail;
  }

  sprintf(search_str, "#nigeb=%s mtime=", filename);
  beginning = strstr(data, search_str);
  if(!beginning){
    fprintf(stderr, "Unable to find %s in the archive.\n", filename);
    error = -ENOENT;
    goto fail;
  }
  beginning = strstr(beginning, "\n");
  if(!beginning){
    fprintf(stderr, "Unable to find %s in the archive.\n", filename);
    error = -ENOENT;
    goto fail;
  }
  beginning++; /* skip newline */

  sprintf(search_str, "#dne=%s", filename);
  end = strstr(beginning, search_str);
  if(!end){
    fprintf(stderr, "Unable to find the end of %s in the archive.\n", filename);
    error = -ENOENT;
    goto fail;
  }

  size = end - beginning;

  *file_data = malloc(size+1);
  if(!file_data){
    fprintf(stderr, "Unable to allocate memory for CCS file.\n");
    error = -ENOMEM;
    goto fail;
  }
  memset(*file_data, 0, size+1);

  memcpy(*file_data, beginning, size);

 fail:
  if(data) free(data);
  return error;
}


static int parse_filedata(ccs_node_t **cn, char *filedata){
  int error=0;
  parser_t p;

  memset(&p, 0, sizeof(p));
  if(!filedata){
    error = -ENODATA;
    goto fail;
  }
  p.db = p.data = filedata;
  p.line =1;
  p.de = (char *)(filedata + strlen(filedata));

  p.tb = p.te = p.db;
  _get_token(&p);
  if(!(*cn = _file(&p))){
    error = -EPARSE;
  }

 fail:
  return error;
}


/*
 * parse_ccs_file
 * @cf:
 *
 * Returns: 0 on success, -EXXX on error
 */
int parse_ccs_file(int fd, ccs_node_t **cn, char *filename){
  int error;
  char *file_data = NULL;

  error = get_ccs_file(fd, &file_data, filename);
  if(error < 0){
    goto fail;
  }
  /* This is where the magic happens */
  error = parse_filedata(cn, file_data);

 fail:
  if(file_data) free(file_data);
  return error;
}


static int _tok_match(const char *str, const char *b, const char *e)
{
  while (*str && (b != e)) {
    if (*str++ != *b++)
      return 0;
  }

  return !(*str || (b != e));
}


ccs_node_t *find_ccs_node(ccs_node_t *cn,
			  const char *path,
			  char sep){
  const char *e;
  while (cn) {
    /* trim any leading slashes */
    while (*path && (*path == sep))
      path++;

    /* find the end of this segment */
    for (e = path; *e && (*e != sep); e++) ;

    /* hunt for the node */
    while (cn) {
      if (_tok_match(cn->key, path, e))
	break;

      cn = cn->sib;
    }

    if (cn && *e)
      cn = cn->child;
    else
      break;  /* don't move into the last node */
    
    path = e;
  }

  return cn;
}


const char *find_ccs_str(ccs_node_t *cn,
			 const char *path,
			 char sep,
			 const char *fail){
  ccs_node_t *n = find_ccs_node(cn, path, sep);

  if (n && n->v && n->v->type == CCS_STRING) {
    return n->v->v.str;
  }

  return fail;
}


int find_ccs_int(ccs_node_t *cn,
		 const char *path,
		 char sep,
		 int fail){
  ccs_node_t *n = find_ccs_node(cn, path, sep);

  if (n && n->v && n->v->type == CCS_INT) {
    return n->v->v.i;
  }

  return fail;
}

#ifndef __KERNEL__
float find_ccs_float(ccs_node_t *cn,
		     const char *path,
		     char sep,
		     float fail){
  ccs_node_t *n = find_ccs_node(cn, path, sep);
  
  if (n && n->v && n->v->type == CCS_FLOAT) {
    return n->v->v.r;
  }

  return fail;
}
#endif

static int _str_in_array(const char *str, const char *values[]){
  int i;

  for (i = 0; values[i]; i++){
    if (!strcasecmp(str, values[i]))
      return 1;
  }
  return 0;
}


static int _str_to_bool(const char *str, int fail){
  static const char *_true_values[]  = { "y", "yes", "on", "true",  NULL };
  static const char *_false_values[] = { "n", "no", "off", "false", NULL };

  if (_str_in_array(str, _true_values))
    return 1;

  if (_str_in_array(str, _false_values))
    return 0;

  return fail;
}

int find_ccs_bool(ccs_node_t *cn,
		  const char *path,
		  char sep,
		  int fail){
  ccs_node_t *n = find_ccs_node(cn, path, sep);
  ccs_value_t *v;

  if (!n)
    return fail;

  v = n->v;

  if(!v)
    return fail;

  switch (v->type) {
  case CCS_INT:
    return v->v.i ? 1 : 0;

  case CCS_STRING:
    return _str_to_bool(v->v.str, fail);
  }

  return fail;
}
