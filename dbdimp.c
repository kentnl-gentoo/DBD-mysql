/*
 *  DBD::mysql - DBI driver for the mysql database
 *
 *  Copyright (c) 2005       Patrick Galbraith
 *  Copyright (c) 2003       Rudolf Lippan
 *  Copyright (c) 1997-2003  Jochen Wiedmann
 *
 *  You may distribute this under the terms of either the GNU General Public
 *  License or the Artistic License, as specified in the Perl README file.
 *
 *  $Id: dbdimp.c 1390 2005-07-07 21:09:21Z capttofu $
 */


#ifdef WIN32
#include "windows.h"
#include "winsock.h"
#endif

#include "dbdimp.h"

#if defined(WIN32)  &&  defined(WORD)
    /*  Don't exactly know who's responsible for defining WORD ... :-(  */
#undef WORD
typedef short WORD;
#endif



DBISTATE_DECLARE;

typedef struct sql_type_info_s
{
    const char *type_name;
    int data_type;
    int column_size;
    const char *literal_prefix;
    const char *literal_suffix;
    const char *create_params;
    int nullable;
    int case_sensitive;
    int searchable;
    int unsigned_attribute;
    int fixed_prec_scale;
    int auto_unique_value;
    const char *local_type_name;
    int minimum_scale;
    int maximum_scale;
    int num_prec_radix;
    int sql_datatype;
    int sql_datetime_sub;
    int interval_precision;
    int native_type;
    int is_num;
} sql_type_info_t;


/*

  This function manually counts the number of placeholders in an SQL statement,
  used for emulated prepare statements < 4.1.3

*/
static int
count_params(char *statement)
{
  char* ptr = statement;
  int num_params = 0;
  char c;

  while ( (c = *ptr++) )
  {
    switch (c) {
    case '`':
    case '"':
    case '\'':
      /* Skip string */
      {
        char end_token = c;
        while ((c = *ptr)  &&  c != end_token)
        {
          if (c == '\\')
            if (! *ptr)
              continue;

          ++ptr;
        }
        if (c)
          ++ptr;
        break;
      }

    case '?':
      ++num_params;
      break;

    default:
      break;
    }
  }
  return num_params;
}

/*
  allocate memory in statement handle per number of placeholders
*/
static imp_sth_ph_t *alloc_param(int num_params)
{
  imp_sth_ph_t *params;

  if (num_params)
    Newz(908, params, num_params, imp_sth_ph_t);
  else
    params= NULL;

  return params;
}


#if MYSQL_VERSION_ID >= SERVER_PREPARE_VERSION
/* 

  allocate memory in MYSQL_BIND bind structure per
  number of placeholders
*/
static MYSQL_BIND *alloc_bind(int num_params)
{
  MYSQL_BIND *bind;

  if (num_params)
    Newz(908, bind, num_params, MYSQL_BIND);
  else
    bind= NULL;

  return bind;
}

/*
  allocate memory in fbind imp_sth_phb_t structure per
  number of placeholders
*/
static imp_sth_phb_t *alloc_fbind(int num_params)
{
  imp_sth_phb_t *fbind;

  if (num_params)
    Newz(908, fbind, num_params, imp_sth_phb_t);
  else
    fbind= NULL;

  return fbind;
}

/*
  alloc memory for imp_sth_fbh_t fbuffer per number of fields
*/
static imp_sth_fbh_t *alloc_fbuffer(int num_fields)
{
  imp_sth_fbh_t *fbh;

  if (num_fields)
    Newz(908, fbh, num_fields, imp_sth_fbh_t);
  else
    fbh= NULL;

  return fbh;
}

/*
  free MYSQL_BIND bind struct
*/
static void FreeBind(MYSQL_BIND* bind)
{
  if (bind)
    Safefree(bind);
  else
    fprintf(stderr,"FREE ERROR BIND!");
}

/*
   free imp_sth_phb_t fbind structure
*/
static void FreeFBind(imp_sth_phb_t *fbind)
{
  if (fbind)
    Safefree(fbind);
  else
    fprintf(stderr,"FREE ERROR  FBIND!");
}

/* 
  free imp_sth_fbh_t fbh structure
*/
static void FreeFBuffer(imp_sth_fbh_t * fbh)
{
  if (fbh)
    Safefree(fbh);
  else
    fprintf(stderr,"FREE ERROR FBUFFER!");
}

#endif

/*
  free statement param structure per num_params
*/
static void
FreeParam(imp_sth_ph_t *params, int num_params)
{
  if (params)
  {
    int i;
    for (i= 0;  i < num_params;  i++)
    {
      imp_sth_ph_t *ph= params+i;
      if (ph->value)
      {
        (void) SvREFCNT_dec(ph->value);
        ph->value= NULL;
      }
    }
    Safefree(params);
  }
}

#if MYSQL_VERSION_ID >= SERVER_PREPARE_VERSION
/* 
  Convert a MySQL type to a type that perl can handle

  NOTE: In the future we may want to return a struct with a lot of
  information for each type
*/

static enum enum_field_types mysql_to_perl_type(enum enum_field_types type)
{
  switch (type) {
  case MYSQL_TYPE_DOUBLE:
  case MYSQL_TYPE_FLOAT:
    return MYSQL_TYPE_DOUBLE;

  case MYSQL_TYPE_SHORT:
  case MYSQL_TYPE_TINY:
  case MYSQL_TYPE_LONG:
  case MYSQL_TYPE_INT24:
  case MYSQL_TYPE_YEAR:
    return MYSQL_TYPE_LONG;

  case MYSQL_TYPE_DECIMAL:
  case MYSQL_TYPE_LONGLONG:			/* No longlong in perl */
  case MYSQL_TYPE_DATE:
  case MYSQL_TYPE_TIME:
  case MYSQL_TYPE_DATETIME:
  case MYSQL_TYPE_NEWDATE:
  case MYSQL_TYPE_VAR_STRING:
  case MYSQL_TYPE_STRING:
  case MYSQL_TYPE_BLOB:
  case MYSQL_TYPE_TIMESTAMP:
  /* case MYSQL_TYPE_UNKNOWN: */
    return MYSQL_TYPE_STRING;

  default:
    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP, "case default for col_type => %d\n", type);
    return MYSQL_TYPE_STRING;    /* MySQL can handle all types as strings */
  }
}
#endif

#if defined(DBD_MYSQL_EMBEDDED)
/* 
  count embedded options
*/
int count_embedded_options(char *st)
{
  int rc;
  char c;
  char *ptr;

  ptr= st;
  rc= 0;

  if (st)
  {
    while ((c= *ptr++))
    {
      if (c == ',')
        rc++;
    }
    rc++;
  }

  return rc;
}

/*
  Free embbedded options
*/
int free_embedded_options(char ** options_list, int options_count)
{
  int i;

  for (i= 0; i < options_count; i++)
  {
    if (options_list[i])
      free(options_list[i]);
  }
  free(options_list);

  return 1;
}

/*
 Print out embbedded option settings

*/
int print_embedded_options(char ** options_list, int options_count)
{
  int i;

  for (i=0; i<options_count; i++)
  {
    if (options_list[i])
        PerlIO_printf(DBILOGFP,
                      "Embedded server, parameter[%d]=%s\n",
                      i, options_list[i]);
  }
  return 1;
}

/*

*/
char **fill_out_embedded_options(char *options,
                                 int options_type,
                                 int slen, int cnt)
{
  int  ind, len;
  char c;
  char *ptr;
  char **options_list= NULL;

  if (!(options_list= (char **) calloc(cnt, sizeof(char *))))
  {
    PerlIO_printf(DBILOGFP,
                  "Initialize embedded server. Out of memory \n");
    return NULL;
  }

  ptr= options;
  ind= 0;

  if (options_type == 0)
  {
    /* server_groups list NULL terminated */
    options_list[cnt]= (char *) NULL;
  }

  if (options_type == 1)
  {
    /* first item in server_options list is ignored. fill it with \0 */
    if (!(options_list[0]= calloc(1,sizeof(char))))
    {
      PerlIO_printf(DBILOGFP,
                    "Initialize embedded server. Out of memory \n");
      return NULL;
    }
    ind++;
  }

  while ((c= *ptr++))
  {
    slen--;
    if (c == ',' || !slen)
    {
      len= ptr - options;
      if (c == ',')
        len--;
      if (!(options_list[ind]=calloc(len+1,sizeof(char))))
      {
        PerlIO_printf(DBILOGFP,
                      "Initialize embedded server. Out of memory\n");
        return NULL;
      }
      strncpy(options_list[ind], options, len);
      ind++;
      options= ptr;
    }
  }
  return options_list;
}
#endif

/* 
  constructs an SQL statement previously prepared with
  actual values replacing placeholders
*/
static char *parse_params(
                          MYSQL *sock,
                          char *statement,
                          STRLEN *slen_ptr,
                          imp_sth_ph_t* params,
                          int num_params,
                          bool bind_type_guessing)
{

  bool seen_neg, seen_dec;
  char *salloc, *statement_ptr;
  char *statement_ptr_end, testchar, *ptr, *valbuf;
  int alen, i, j;
  int slen= *slen_ptr;
  int limit_flag= 0;
  STRLEN vallen;
  imp_sth_ph_t *ph;

  /* I want to add mysql DBUG_ENTER (DBUG_<> macros) */
  if (dbis->debug >= 2)
    PerlIO_printf(DBILOGFP,
                  "---> parse_params with statement %s num params %d\n",
                  statement, num_params);

  if (num_params == 0)
  {
    return NULL;
  }

  while (isspace(*statement))
  {
    ++statement;
    --slen;
  }
  if (dbis->debug >= 2)
    PerlIO_printf(DBILOGFP,
                  "     parse_params slen %d\n", slen);

  /* Calculate the number of bytes being allocated for the statement */
  alen= slen;

  for (i= 0, ph= params; i < num_params; i++, ph++)
  {
    if (!ph->value  ||  !SvOK(ph->value))
      alen+= 3;  /* Erase '?', insert 'NULL' */
    else
    {
      valbuf= SvPV(ph->value, vallen);
      alen+= 2+vallen+1;
      /* this will most likely not happen since line 214 */
      /* of mysql.xs hardcodes all types to SQL_VARCHAR */
      if (!ph->type)
      {
        if ( bind_type_guessing > 1 )
        {
          valbuf= SvPV(ph->value, vallen);
          ph->type= SQL_INTEGER;

          /* patch from Dragonchild */
          seen_neg= 0;
          seen_dec= 0;
          for (j= 0; j < vallen; ++j)
          {
            testchar= *(valbuf+j);
            if ('-' == testchar)
            {
              if (seen_neg)
              {
                ph->type= SQL_VARCHAR;
                break;
              }
              else if (j)
              {
                ph->type= SQL_VARCHAR;
                break;
              }
              seen_neg= 1;
            }
            else if ('.' == testchar)
            {
              if (seen_dec)
              {
                ph->type= SQL_VARCHAR;
                break;
              }
              seen_dec= 1;
            }
            else if (!isdigit(testchar))
            {
              ph->type= SQL_VARCHAR;
              break;
            }
          }
        }
        else if (bind_type_guessing)
          ph->type= SvNIOK(ph->value) ? SQL_INTEGER : SQL_VARCHAR;
        else
          ph->type= SQL_VARCHAR;
      }
    }
  }

  /* Allocate memory, why *2, well, because we have ptr and statement_ptr */
  New(908, salloc, alen*2, char);
  ptr= salloc;

  i= 0;
 /* Now create the statement string; compare count_params above */
  statement_ptr_end= (statement_ptr= statement)+ slen;

  while (statement_ptr < statement_ptr_end)
  {
    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP,
                    "     parse_params statement_ptr %08lx = %s \
                    statement_ptr_end %08lx\n",
                    statement_ptr, statement_ptr, statement_ptr_end);
    /* LIMIT should be the last part of the query, in most cases */
    if (! limit_flag)
    {
      /*
        it would be good to be able to handle any number of cases and orders
      */
      if ((*statement_ptr == 'l' || *statement_ptr == 'L') &&
          (!strncmp(statement_ptr+1, "imit ?", 6) ||
           !strncmp(statement_ptr+1, "IMIT ?", 6)))
      {
        limit_flag = 1;
      }
    }
    switch (*statement_ptr)
    {
      case '`':
      case '\'':
      case '"':
      /* Skip string */
      {
        char endToken = *statement_ptr++;
        *ptr++ = endToken;
        while (statement_ptr != statement_ptr_end &&
               *statement_ptr != endToken)
        {
          if (*statement_ptr == '\\')
          {
            *ptr++ = *statement_ptr++;
            if (statement_ptr == statement_ptr_end)
	      break;
	  }
          *ptr++= *statement_ptr++;
	}
	if (statement_ptr != statement_ptr_end)
          *ptr++= *statement_ptr++;
      }
      break;

      case '?':
        /* Insert parameter */
        statement_ptr++;
        if (i >= num_params)
        {
          break;
        }

        ph = params+ (i++);
        if (!ph->value  ||  !SvOK(ph->value))
        {
          *ptr++ = 'N';
          *ptr++ = 'U';
          *ptr++ = 'L';
          *ptr++ = 'L';
        }
        else
        {
          int is_num = FALSE;
          int c;

          valbuf= SvPV(ph->value, vallen);
          if (valbuf)
          {
            switch (ph->type)
            {
              case SQL_NUMERIC:
              case SQL_DECIMAL:
              case SQL_INTEGER:
              case SQL_SMALLINT:
              case SQL_FLOAT:
              case SQL_REAL:
              case SQL_DOUBLE:
              case SQL_BIGINT:
              case SQL_TINYINT:
                is_num = TRUE;
                break;
            }

            /* we're at the end of the query, so any placeholders if */
            /* after a LIMIT clause will be numbers and should not be quoted */
            if (limit_flag == 1)
              is_num = TRUE;

            if (!is_num)
            {
              *ptr++ = '\'';
              ptr += mysql_real_escape_string(sock, ptr, valbuf, vallen);
              *ptr++ = '\'';
            }
            else
            {
              while (vallen--)
              {
		c = *valbuf++;
		if ((c < '0' || c > '9') && c != ' ')
		  break;
		*ptr++= c;
              }
            }
          }
        }
        break;

	/* in case this is a nested LIMIT */
      case ')':
        limit_flag = 0;
	*ptr++ = *statement_ptr++;
        break;

      default:
        *ptr++ = *statement_ptr++;
        break;

    }
  }

  *slen_ptr = ptr - salloc;
  *ptr++ = '\0';
  if (dbis->debug >= 2)
    PerlIO_printf(DBILOGFP, "<--- parse_params\n");

  return(salloc);
}

int bind_param(imp_sth_ph_t *ph, SV *value, IV sql_type)
{
  if (ph->value)
    (void) SvREFCNT_dec(ph->value);

  ph->value = newSVsv(value);

  if (sql_type)
    ph->type = sql_type;

  return TRUE;
}

static const sql_type_info_t SQL_GET_TYPE_INFO_values[]= {
  { "varchar",    SQL_VARCHAR,                    255, "'",  "'",  "max length",
    1, 0, 3, 0, 0, 0, "variable length string",
    0, 0, 0,
    SQL_VARCHAR, 0, 0,
    FIELD_TYPE_VAR_STRING,  0,
    /* 0 */
  },
  { "decimal",   SQL_DECIMAL,                      15, NULL, NULL, "precision,scale",
    1, 0, 3, 0, 0, 0, "double",
    0, 6, 2,
    SQL_DECIMAL, 0, 0,
    FIELD_TYPE_DECIMAL,     1
    /* 1 */
  },
  { "tinyint",   SQL_TINYINT,                       3, NULL, NULL, NULL,
    1, 0, 3, 0, 0, 0, "Tiny integer",
    0, 0, 10,
    SQL_TINYINT, 0, 0,
    FIELD_TYPE_TINY,        1
    /* 2 */
  },
  { "smallint",  SQL_SMALLINT,                      5, NULL, NULL, NULL,
    1, 0, 3, 0, 0, 0, "Short integer",
    0, 0, 10,
    SQL_SMALLINT, 0, 0,
    FIELD_TYPE_SHORT,       1
    /* 3 */
  },
  { "integer",   SQL_INTEGER,                      10, NULL, NULL, NULL,
    1, 0, 3, 0, 0, 0, "integer",
    0, 0, 10,
    SQL_INTEGER, 0, 0,
    FIELD_TYPE_LONG,        1
    /* 4 */
  },
  { "float",     SQL_REAL,                          7,  NULL, NULL, NULL,
    1, 0, 0, 0, 0, 0, "float",
    0, 2, 10,
    SQL_FLOAT, 0, 0,
    FIELD_TYPE_FLOAT,       1
    /* 5 */
  },
  { "double",    SQL_FLOAT,                       15,  NULL, NULL, NULL,
    1, 0, 3, 0, 0, 0, "double",
    0, 4, 2,
    SQL_FLOAT, 0, 0,
    FIELD_TYPE_DOUBLE,      1
    /* 6 */
  },
  { "double",    SQL_DOUBLE,                       15,  NULL, NULL, NULL,
    1, 0, 3, 0, 0, 0, "double",
    0, 4, 10,
    SQL_DOUBLE, 0, 0,
    FIELD_TYPE_DOUBLE,      1
    /* 6 */
  },
  /*
    FIELD_TYPE_NULL ?
  */
  { "timestamp", SQL_TIMESTAMP,                    14, "'", "'", NULL,
    0, 0, 3, 0, 0, 0, "timestamp",
    0, 0, 0,
    SQL_TIMESTAMP, 0, 0,
    FIELD_TYPE_TIMESTAMP,   0
    /* 7 */
  },
  { "bigint",    SQL_BIGINT,                       19, NULL, NULL, NULL,
    1, 0, 3, 0, 0, 0, "Longlong integer",
    0, 0, 10,
    SQL_BIGINT, 0, 0,
    FIELD_TYPE_LONGLONG,    1
    /* 8 */
  },
  { "middleint", SQL_INTEGER,                       8, NULL, NULL, NULL,
    1, 0, 3, 0, 0, 0, "Medium integer",
    0, 0, 10,
    SQL_INTEGER, 0, 0,
    FIELD_TYPE_INT24,       1
    /* 9 */
  },
  { "date",      SQL_DATE,                         10, "'",  "'",  NULL,
    1, 0, 3, 0, 0, 0, "date",
    0, 0, 0,
    SQL_DATE, 0, 0,
    FIELD_TYPE_DATE,        0
    /* 10 */
  },
  { "time",      SQL_TIME,                          6, "'",  "'",  NULL,
    1, 0, 3, 0, 0, 0, "time",
    0, 0, 0,
    SQL_TIME, 0, 0,
    FIELD_TYPE_TIME,        0
    /* 11 */
  },
  { "datetime",  SQL_TIMESTAMP,                    21, "'",  "'",  NULL,
    1, 0, 3, 0, 0, 0, "datetime",
    0, 0, 0,
    SQL_TIMESTAMP, 0, 0,
    FIELD_TYPE_DATETIME,    0
    /* 12 */
  },
  { "year",      SQL_SMALLINT,                      4, NULL, NULL, NULL,
    1, 0, 3, 0, 0, 0, "year",
    0, 0, 10,
    SQL_SMALLINT, 0, 0,
    FIELD_TYPE_YEAR,        0
    /* 13 */
  },
  { "date",      SQL_DATE,                         10, "'",  "'",  NULL,
    1, 0, 3, 0, 0, 0, "date",
    0, 0, 0,
    SQL_DATE, 0, 0,
    FIELD_TYPE_NEWDATE,     0
    /* 14 */
  },
  { "enum",      SQL_VARCHAR,                     255, "'",  "'",  NULL,
    1, 0, 1, 0, 0, 0, "enum(value1,value2,value3...)",
    0, 0, 0,
    0, 0, 0,
    FIELD_TYPE_ENUM,        0
    /* 15 */
  },
  { "set",       SQL_VARCHAR,                     255, "'",  "'",  NULL,
    1, 0, 1, 0, 0, 0, "set(value1,value2,value3...)",
    0, 0, 0,
    0, 0, 0,
    FIELD_TYPE_SET,         0
    /* 16 */
  },
  { "blob",       SQL_LONGVARBINARY,              65535, "'",  "'",  NULL,
    1, 0, 3, 0, 0, 0, "binary large object (0-65535)",
    0, 0, 0,
    SQL_LONGVARBINARY, 0, 0,
    FIELD_TYPE_BLOB,        0
    /* 17 */
  },
  { "tinyblob",  SQL_VARBINARY,                 255, "'",  "'",  NULL,
    1, 0, 3, 0, 0, 0, "binary large object (0-255) ",
    0, 0, 0,
    SQL_VARBINARY, 0, 0,
    FIELD_TYPE_TINY_BLOB,   0
    /* 18 */
  },
  { "mediumblob", SQL_LONGVARBINARY,           16777215, "'",  "'",  NULL,
    1, 0, 3, 0, 0, 0, "binary large object",
    0, 0, 0,
    SQL_LONGVARBINARY, 0, 0,
    FIELD_TYPE_MEDIUM_BLOB, 0
    /* 19 */
  },
  { "longblob",   SQL_LONGVARBINARY,         2147483647, "'",  "'",  NULL,
    1, 0, 3, 0, 0, 0, "binary large object, use mediumblob instead",
    0, 0, 0,
    SQL_LONGVARBINARY, 0, 0,
    FIELD_TYPE_LONG_BLOB,   0
    /* 20 */
  },
  { "char",       SQL_CHAR,                       255, "'",  "'",  "max length",
    1, 0, 3, 0, 0, 0, "string",
    0, 0, 0,
    SQL_CHAR, 0, 0,
    FIELD_TYPE_STRING,      0
    /* 21 */
  },

  { "decimal",            SQL_NUMERIC,            15,  NULL, NULL, "precision,scale",
    1, 0, 3, 0, 0, 0, "double",
    0, 6, 2,
    SQL_NUMERIC, 0, 0,
    FIELD_TYPE_DECIMAL,     1
  },
  /*
  { "tinyint",            SQL_BIT,                  3, NULL, NULL, NULL,
    1, 0, 1, 0, 0, 0, "Tiny integer",
    0, 0, 10, FIELD_TYPE_TINY,        1
  },
  */
  { "tinyint unsigned",   SQL_TINYINT,              3, NULL, NULL, NULL,
    1, 0, 3, 1, 0, 0, "Tiny integer unsigned",
    0, 0, 10,
    SQL_TINYINT, 0, 0,
    FIELD_TYPE_TINY,        1
  },
  { "smallint unsigned",  SQL_SMALLINT,             5, NULL, NULL, NULL,
    1, 0, 3, 1, 0, 0, "Short integer unsigned",
    0, 0, 10,
    SQL_SMALLINT, 0, 0,
    FIELD_TYPE_SHORT,       1
  },
  { "middleint unsigned", SQL_INTEGER,              8, NULL, NULL, NULL,
    1, 0, 3, 1, 0, 0, "Medium integer unsigned",
    0, 0, 10,
    SQL_INTEGER, 0, 0,
    FIELD_TYPE_INT24,       1
  },
  { "int unsigned",       SQL_INTEGER,             10, NULL, NULL, NULL,
    1, 0, 3, 1, 0, 0, "integer unsigned",
    0, 0, 10,
    SQL_INTEGER, 0, 0,
    FIELD_TYPE_LONG,        1
  },
  { "int",                SQL_INTEGER,             10, NULL, NULL, NULL,
    1, 0, 3, 0, 0, 0, "integer",
    0, 0, 10,
    SQL_INTEGER, 0, 0,
    FIELD_TYPE_LONG,        1
  },
  { "integer unsigned",   SQL_INTEGER,             10, NULL, NULL, NULL,
    1, 0, 3, 1, 0, 0, "integer",
    0, 0, 10,
    SQL_INTEGER, 0, 0,
    FIELD_TYPE_LONG,        1
  },
  { "bigint unsigned",    SQL_BIGINT,              20, NULL, NULL, NULL,
    1, 0, 3, 1, 0, 0, "Longlong integer unsigned",
    0, 0, 10,
    SQL_BIGINT, 0, 0,
    FIELD_TYPE_LONGLONG,    1
  },
  { "text",               SQL_LONGVARCHAR,      65535, "'",  "'",  NULL,
    1, 0, 3, 0, 0, 0, "large text object (0-65535)",
    0, 0, 0,
    SQL_LONGVARCHAR, 0, 0,
    FIELD_TYPE_BLOB,        0
  },
  { "mediumtext",         SQL_LONGVARCHAR,   16777215, "'",  "'",  NULL,
    1, 0, 3, 0, 0, 0, "large text object",
    0, 0, 0,
    SQL_LONGVARCHAR, 0, 0,
    FIELD_TYPE_MEDIUM_BLOB, 0
  }


 /* BEGIN MORE STUFF */
,


  { "mediumint unsigned auto_increment", SQL_INTEGER, 8, NULL, NULL, NULL,
    0, 0, 3, 1, 0, 1, "Medium integer unsigned auto_increment", 0, 0, 10,
    SQL_INTEGER, 0, 0, FIELD_TYPE_INT24, 1,
  },

  { "tinyint unsigned auto_increment", SQL_TINYINT, 3, NULL, NULL, NULL,
    0, 0, 3, 1, 0, 1, "tinyint unsigned auto_increment", 0, 0, 10,
    SQL_TINYINT, 0, 0, FIELD_TYPE_TINY, 1
  },

  { "smallint auto_increment", SQL_SMALLINT, 5, NULL, NULL, NULL,
    0, 0, 3, 0, 0, 1, "smallint auto_increment", 0, 0, 10,
    SQL_SMALLINT, 0, 0, FIELD_TYPE_SHORT, 1
  },

  { "int unsigned auto_increment", SQL_INTEGER, 10, NULL, NULL, NULL,
    0, 0, 3, 1, 0, 1, "integer unsigned auto_increment", 0, 0, 10,
    SQL_INTEGER, 0, 0, FIELD_TYPE_LONG, 1
  },

  { "mediumint", SQL_INTEGER, 7, NULL, NULL, NULL,
    1, 0, 3, 0, 0, 0, "Medium integer", 0, 0, 10,
    SQL_INTEGER, 0, 0, FIELD_TYPE_INT24, 1
  },

  { "bit", SQL_BIT, 1, NULL, NULL, NULL,
    1, 0, 3, 0, 0, 0, "char(1)", 0, 0, 0,
    SQL_BIT, 0, 0, FIELD_TYPE_TINY, 0
  },

  { "numeric", SQL_NUMERIC, 19, NULL, NULL, "precision,scale",
    1, 0, 3, 0, 0, 0, "numeric", 0, 19, 10,
    SQL_NUMERIC, 0, 0, FIELD_TYPE_DECIMAL, 1,
  },

  { "integer unsigned auto_increment", SQL_INTEGER, 10, NULL, NULL, NULL,
    0, 0, 3, 1, 0, 1, "integer unsigned auto_increment", 0, 0, 10,
    SQL_INTEGER, 0, 0, FIELD_TYPE_LONG, 1,
  },

  { "mediumint unsigned", SQL_INTEGER, 8, NULL, NULL, NULL,
    1, 0, 3, 1, 0, 0, "Medium integer unsigned", 0, 0, 10,
    SQL_INTEGER, 0, 0, FIELD_TYPE_INT24, 1
  },

  { "smallint unsigned auto_increment", SQL_SMALLINT, 5, NULL, NULL, NULL,
    0, 0, 3, 1, 0, 1, "smallint unsigned auto_increment", 0, 0, 10,
    SQL_SMALLINT, 0, 0, FIELD_TYPE_SHORT, 1
  },

  { "int auto_increment", SQL_INTEGER, 10, NULL, NULL, NULL,
    0, 0, 3, 0, 0, 1, "integer auto_increment", 0, 0, 10,
    SQL_INTEGER, 0, 0, FIELD_TYPE_LONG, 1
  },

  { "long varbinary", SQL_LONGVARBINARY, 16777215, "0x", NULL, NULL,
    1, 0, 3, 0, 0, 0, "mediumblob", 0, 0, 0,
    SQL_LONGVARBINARY, 0, 0, FIELD_TYPE_LONG_BLOB, 0
  },

  { "double auto_increment", SQL_FLOAT, 15, NULL, NULL, NULL,
    0, 0, 3, 0, 0, 1, "double auto_increment", 0, 4, 2,
    SQL_FLOAT, 0, 0, FIELD_TYPE_DOUBLE, 1
  },

  { "double auto_increment", SQL_DOUBLE, 15, NULL, NULL, NULL,
    0, 0, 3, 0, 0, 1, "double auto_increment", 0, 4, 10,
    SQL_DOUBLE, 0, 0, FIELD_TYPE_DOUBLE, 1
  },

  { "integer auto_increment", SQL_INTEGER, 10, NULL, NULL, NULL,
    0, 0, 3, 0, 0, 1, "integer auto_increment", 0, 0, 10,
    SQL_INTEGER, 0, 0, FIELD_TYPE_LONG, 1,
  },

  { "bigint auto_increment", SQL_BIGINT, 19, NULL, NULL, NULL,
    0, 0, 3, 0, 0, 1, "bigint auto_increment", 0, 0, 10,
    SQL_BIGINT, 0, 0, FIELD_TYPE_LONGLONG, 1
  },

  { "bit auto_increment", SQL_BIT, 1, NULL, NULL, NULL,
    0, 0, 3, 0, 0, 1, "char(1) auto_increment", 0, 0, 0,
    SQL_BIT, 0, 0, FIELD_TYPE_TINY, 1
  },

  { "mediumint auto_increment", SQL_INTEGER, 7, NULL, NULL, NULL,
    0, 0, 3, 0, 0, 1, "Medium integer auto_increment", 0, 0, 10,
    SQL_INTEGER, 0, 0, FIELD_TYPE_INT24, 1
  },

  { "float auto_increment", SQL_REAL, 7, NULL, NULL, NULL,
    0, 0, 0, 0, 0, 1, "float auto_increment", 0, 2, 10,
    SQL_FLOAT, 0, 0, FIELD_TYPE_FLOAT, 1
  },

  { "long varchar", SQL_LONGVARCHAR, 16777215, "'", "'", NULL,
    1, 0, 3, 0, 0, 0, "mediumtext", 0, 0, 0,
    SQL_LONGVARCHAR, 0, 0, FIELD_TYPE_MEDIUM_BLOB, 1
  },

  { "tinyint auto_increment", SQL_TINYINT, 3, NULL, NULL, NULL,
    0, 0, 3, 0, 0, 1, "tinyint auto_increment", 0, 0, 10,
    SQL_TINYINT, 0, 0, FIELD_TYPE_TINY, 1
  },

  { "bigint unsigned auto_increment", SQL_BIGINT, 20, NULL, NULL, NULL,
    0, 0, 3, 1, 0, 1, "bigint unsigned auto_increment", 0, 0, 10,
    SQL_BIGINT, 0, 0, FIELD_TYPE_LONGLONG, 1
  },

/* END MORE STUFF */
};

/* 
  static const sql_type_info_t* native2sql (int t)
*/
static const sql_type_info_t *native2sql(int t)
{
  switch (t) {
    case FIELD_TYPE_VAR_STRING:  return &SQL_GET_TYPE_INFO_values[0];
    case FIELD_TYPE_DECIMAL:     return &SQL_GET_TYPE_INFO_values[1];
    case FIELD_TYPE_TINY:        return &SQL_GET_TYPE_INFO_values[2];
    case FIELD_TYPE_SHORT:       return &SQL_GET_TYPE_INFO_values[3];
    case FIELD_TYPE_LONG:        return &SQL_GET_TYPE_INFO_values[4];
    case FIELD_TYPE_FLOAT:       return &SQL_GET_TYPE_INFO_values[5];

    /* 6  */
    case FIELD_TYPE_DOUBLE:      return &SQL_GET_TYPE_INFO_values[7];
    case FIELD_TYPE_TIMESTAMP:   return &SQL_GET_TYPE_INFO_values[8];
    case FIELD_TYPE_LONGLONG:    return &SQL_GET_TYPE_INFO_values[9];
    case FIELD_TYPE_INT24:       return &SQL_GET_TYPE_INFO_values[10];
    case FIELD_TYPE_DATE:        return &SQL_GET_TYPE_INFO_values[11];
    case FIELD_TYPE_TIME:        return &SQL_GET_TYPE_INFO_values[12];
    case FIELD_TYPE_DATETIME:    return &SQL_GET_TYPE_INFO_values[13];
    case FIELD_TYPE_YEAR:        return &SQL_GET_TYPE_INFO_values[14];
    case FIELD_TYPE_NEWDATE:     return &SQL_GET_TYPE_INFO_values[15];
    case FIELD_TYPE_ENUM:        return &SQL_GET_TYPE_INFO_values[16];
    case FIELD_TYPE_SET:         return &SQL_GET_TYPE_INFO_values[17];
    case FIELD_TYPE_BLOB:        return &SQL_GET_TYPE_INFO_values[18];
    case FIELD_TYPE_TINY_BLOB:   return &SQL_GET_TYPE_INFO_values[19];
    case FIELD_TYPE_MEDIUM_BLOB: return &SQL_GET_TYPE_INFO_values[20];
    case FIELD_TYPE_LONG_BLOB:   return &SQL_GET_TYPE_INFO_values[21];
    case FIELD_TYPE_STRING:      return &SQL_GET_TYPE_INFO_values[22];
    default:                     return &SQL_GET_TYPE_INFO_values[0];
  }
}


#define SQL_GET_TYPE_INFO_num \
	(sizeof(SQL_GET_TYPE_INFO_values)/sizeof(sql_type_info_t))


/***************************************************************************
 *
 *  Name:    dbd_init
 *
 *  Purpose: Called when the driver is installed by DBI
 *
 *  Input:   dbistate - pointer to the DBIS variable, used for some
 *               DBI internal things
 *
 *  Returns: Nothing
 *
 **************************************************************************/

void dbd_init(dbistate_t* dbistate)
{
    DBIS = dbistate;
}


/**************************************************************************
 *
 *  Name:    do_error, do_warn
 *
 *  Purpose: Called to associate an error code and an error message
 *           to some handle
 *
 *  Input:   h - the handle in error condition
 *           rc - the error code
 *           what - the error message
 *
 *  Returns: Nothing
 *
 **************************************************************************/

void do_error(SV* h, int rc, const char* what)
{
  D_imp_xxh(h);
  STRLEN lna;

  SV *errstr = DBIc_ERRSTR(imp_xxh);
  sv_setiv(DBIc_ERR(imp_xxh), (IV)rc);	/* set err early	*/
  sv_setpv(errstr, what);
  DBIh_EVENT2(h, ERROR_event, DBIc_ERR(imp_xxh), errstr);
  if (dbis->debug >= 2)
    PerlIO_printf(DBILOGFP, "%s error %d recorded: %s\n",
    what, rc, SvPV(errstr,lna));
}

/*
  void do_warn(SV* h, int rc, char* what)
*/
void do_warn(SV* h, int rc, char* what)
{
  D_imp_xxh(h);
  STRLEN lna;

  SV *errstr = DBIc_ERRSTR(imp_xxh);
  sv_setiv(DBIc_ERR(imp_xxh), (IV)rc);	/* set err early	*/
  sv_setpv(errstr, what);
  DBIh_EVENT2(h, WARN_event, DBIc_ERR(imp_xxh), errstr);
  if (dbis->debug >= 2)
    PerlIO_printf(DBILOGFP, "%s warning %d recorded: %s\n",
    what, rc, SvPV(errstr,lna));
  warn("%s", what);
}
/* }}} */

#if defined(DBD_MYSQL_EMBEDDED)
 #define DBD_MYSQL_NAMESPACE "DBD::mysqlEmb::QUIET";
#else
 #define DBD_MYSQL_NAMESPACE "DBD::mysql::QUIET";
#endif

#define doquietwarn(s) \
  { \
    SV* sv = perl_get_sv(DBD_MYSQL_NAMESPACE, FALSE);  \
    if (!sv  ||  !SvTRUE(sv)) { \
      warn s; \
    } \
  }


/***************************************************************************
 *
 *  Name:    mysql_dr_connect
 *
 *  Purpose: Replacement for mysql_connect
 *
 *  Input:   MYSQL* sock - Pointer to a MYSQL structure being
 *             initialized
 *           char* mysql_socket - Name of a UNIX socket being used
 *             or NULL
 *           char* host - Host name being used or NULL for localhost
 *           char* port - Port number being used or NULL for default
 *           char* user - User name being used or NULL
 *           char* password - Password being used or NULL
 *           char* dbname - Database name being used or NULL
 *           char* imp_dbh - Pointer to internal dbh structure
 *
 *  Returns: The sock argument for success, NULL otherwise;
 *           you have to call do_error in the latter case.
 *
 **************************************************************************/

MYSQL* mysql_dr_connect(SV* dbh, MYSQL* sock, char* mysql_socket, char* host,
			char* port, char* user, char* password,
			char* dbname, imp_dbh_t *imp_dbh) {
  int portNr;
  MYSQL* result;

  /* per Monty, already in client.c in API */
  /* but still not exist in libmysqld.c */
#if defined(DBD_MYSQL_EMBEDDED)
   if (host && !*host) host = NULL;
#endif

  portNr= (port && *port) ? atoi(port) : 0;

  /* already in client.c in API */
  /* if (user && !*user) user = NULL; */
  /* if (password && !*password) password = NULL; */
 
  if (dbis->debug >= 2)
    PerlIO_printf(DBILOGFP,
		  "imp_dbh->mysql_dr_connect: host = |%s|, port = %d," \
		  " uid = %s, pwd = %s\n",
		  host ? host : "NULL", portNr,
		  user ? user : "NULL",
		  password ? password : "NULL");
 
  {

#if defined(DBD_MYSQL_EMBEDDED)
    if (imp_dbh)
    {
      D_imp_drh_from_dbh;
      SV* sv = DBIc_IMP_DATA(imp_dbh);

      if (sv  &&  SvROK(sv))
      {
        SV** svp;
        STRLEN lna;
        char * options;
        int server_args_cnt= 0;
        int server_groups_cnt= 0;
        int rc= 0;

        char ** server_args = NULL;
        char ** server_groups = NULL;

        HV* hv = (HV*) SvRV(sv);

        if (SvTYPE(hv) != SVt_PVHV)
          return NULL;

        if (!imp_drh->embedded.state)
        {
          /* Init embedded server */
          if ((svp = hv_fetch(hv, "mysql_embedded_groups", 21, FALSE))  &&
              *svp  &&  SvTRUE(*svp))
          {
            options = SvPV(*svp, lna);
            imp_drh->embedded.groups=newSVsv(*svp);

            if ((server_groups_cnt=count_embedded_options(options)))
            {
              /* number of server_groups always server_groups+1 */
              server_groups=fill_out_embedded_options(options, 0, (int)lna, ++server_groups_cnt);
              if (dbis->debug >= 2)
              {
                PerlIO_printf(DBILOGFP, "Groups names passed to embedded server:\n");
                print_embedded_options(server_groups, server_groups_cnt);
              }
            }
          }
 
          if ((svp = hv_fetch(hv, "mysql_embedded_options", 22, FALSE))  &&
              *svp  &&  SvTRUE(*svp))
          {
            options = SvPV(*svp, lna);
            imp_drh->embedded.args=newSVsv(*svp);

            if ((server_args_cnt=count_embedded_options(options)))
            {
              /* number of server_options always server_options+1 */
              server_args=fill_out_embedded_options(options, 1, (int)lna, ++server_args_cnt);
              if (dbis->debug >= 2)
              {
                PerlIO_printf(DBILOGFP, "Server options passed to embedded server:\n");
                print_embedded_options(server_args, server_args_cnt);
              }
            }
          }
          if (mysql_server_init(server_args_cnt, server_args, server_groups))
          {
            do_warn(dbh, AS_ERR_EMBEDDED, "Embedded server was not started. Could not initialize environment.");
            return NULL;            
          }
          imp_drh->embedded.state=1;
 
          if (server_args_cnt)
            free_embedded_options(server_args, server_args_cnt);
          if (server_groups_cnt)
            free_embedded_options(server_groups, server_groups_cnt);
        }
        else
        {
         /*
          * Check if embedded parameters passed to connect() differ from
          * first ones
          */

          if ( ((svp = hv_fetch(hv, "mysql_embedded_groups", 21, FALSE)) &&
            *svp  &&  SvTRUE(*svp)))
            rc =+ abs(sv_cmp(*svp, imp_drh->embedded.groups));

          if ( ((svp = hv_fetch(hv, "mysql_embedded_options", 22, FALSE)) &&
            *svp  &&  SvTRUE(*svp)) )
            rc =+ abs(sv_cmp(*svp, imp_drh->embedded.args));

          if (rc)
          {
            do_warn(dbh, AS_ERR_EMBEDDED, "Embedded server was already started. You cannot pass init parameters to embedded server once");
            return NULL;
          }
        }
      }
    }
#endif

#ifdef MYSQL_NO_CLIENT_FOUND_ROWS
    unsigned int client_flag = 0;
#else
    unsigned int client_flag = CLIENT_FOUND_ROWS;
#endif
    mysql_init(sock);
   
    if (imp_dbh)
    {
      SV* sv = DBIc_IMP_DATA(imp_dbh);

      DBIc_set(imp_dbh, DBIcf_AutoCommit, &sv_yes);
      if (sv  &&  SvROK(sv))
      {
	HV* hv = (HV*) SvRV(sv);
	SV** svp;
	STRLEN lna;
	
	if ((svp = hv_fetch(hv, "mysql_compression", 17, FALSE))  &&
	    *svp && SvTRUE(*svp))
        {
	  if (dbis->debug >= 2)
	    PerlIO_printf(DBILOGFP,
			  "imp_dbh->mysql_dr_connect: Enabling" \
			  " compression.\n");
	  mysql_options(sock, MYSQL_OPT_COMPRESS, NULL);
	}
	if ((svp = hv_fetch(hv, "mysql_connect_timeout", 21, FALSE))
	    &&  *svp  &&  SvTRUE(*svp))
        {
	  int to = SvIV(*svp);
	  if (dbis->debug >= 2)
	    PerlIO_printf(DBILOGFP,
			  "imp_dbh->mysql_dr_connect: Setting" \
			  " connect timeout (%d).\n",to);
	  mysql_options(sock, MYSQL_OPT_CONNECT_TIMEOUT,
			(const char *)&to);
	}
	if ((svp = hv_fetch(hv, "mysql_read_default_file", 23, FALSE)) &&
	    *svp  &&  SvTRUE(*svp))
        {
	  char* df = SvPV(*svp, lna);
	  if (dbis->debug >= 2)
	    PerlIO_printf(DBILOGFP,
			  "imp_dbh->mysql_dr_connect: Reading" \
			  " default file %s.\n", df);
	  mysql_options(sock, MYSQL_READ_DEFAULT_FILE, df);
	}
	if ((svp = hv_fetch(hv, "mysql_read_default_group", 24,
			    FALSE))  &&
	    *svp  &&  SvTRUE(*svp)) {
	  char* gr = SvPV(*svp, lna);
	  if (dbis->debug >= 2)
	    PerlIO_printf(DBILOGFP,
			  "imp_dbh->mysql_dr_connect: Using" \
			  " default group %s.\n", gr);

	  mysql_options(sock, MYSQL_READ_DEFAULT_GROUP, gr);
	}
	if ((svp = hv_fetch(hv, "mysql_client_found_rows", 23, FALSE)) && *svp)
        {
	  if (SvTRUE(*svp))
	    client_flag |= CLIENT_FOUND_ROWS;
          else
            client_flag &= ~CLIENT_FOUND_ROWS;
	}
	if ((svp = hv_fetch(hv, "mysql_use_result", 16, FALSE)) && *svp)
        {
          imp_dbh->use_mysql_use_result = SvTRUE(*svp);
	  if (dbis->debug >= 2)
	    PerlIO_printf(DBILOGFP,
			  "imp_dbh->use_mysql_use_result: %d\n",
                          imp_dbh->use_mysql_use_result);
        }


	/* took out  client_flag |= CLIENT_PROTOCOL_41; */
	/* because libmysql.c already sets this no matter what */

#if MYSQL_VERSION_ID >=SERVER_PREPARE_VERSION

	if ((svp = hv_fetch(hv, "mysql_server_prepare", 20,
			    FALSE))  &&  *svp) {
	  if (SvTRUE(*svp))
          {
	    client_flag |= CLIENT_PROTOCOL_41;
            imp_dbh->use_server_side_prepare = TRUE;
	  }
          else
          {
	    client_flag &= ~CLIENT_PROTOCOL_41;
            imp_dbh->use_server_side_prepare = FALSE;
	  }
	  if (dbis->debug >= 2)
	    PerlIO_printf(DBILOGFP,
			  "imp_dbh->use_server_side_prepare: %d",
                          imp_dbh->use_server_side_prepare);
	}
#endif

#if defined(DBD_MYSQL_WITH_SSL) && !defined(DBD_MYSQL_EMBEDDED) && \
    (defined(CLIENT_SSL) || (MYSQL_VERSION_ID >= 40000))
	if ((svp = hv_fetch(hv, "mysql_ssl", 9, FALSE))  &&  *svp)
        {
	  if (SvTRUE(*svp))
          {
	    char* client_key = NULL;
	    char* client_cert = NULL;
	    char* ca_file = NULL;
	    char* ca_path = NULL;
	    char* cipher = NULL;
	    STRLEN lna;
	    if ((svp = hv_fetch(hv, "mysql_ssl_client_key", 20, FALSE)) && *svp)
	      client_key = SvPV(*svp, lna);

	    if ((svp = hv_fetch(hv, "mysql_ssl_client_cert", 21, FALSE)) &&
                *svp)
	      client_cert = SvPV(*svp, lna);

	    if ((svp = hv_fetch(hv, "mysql_ssl_ca_file", 17, FALSE)) &&
		 *svp)
	      ca_file = SvPV(*svp, lna);

	    if ((svp = hv_fetch(hv, "mysql_ssl_ca_path", 17, FALSE)) &&
                *svp) 
	      ca_path = SvPV(*svp, lna);

	    if ((svp = hv_fetch(hv, "mysql_ssl_cipher", 16, FALSE)) &&
		*svp)
	      cipher = SvPV(*svp, lna);

	    mysql_ssl_set(sock, client_key, client_cert, ca_file,
			  ca_path, cipher);
	    client_flag |= CLIENT_SSL;
	  }
	}
#endif
#if (MYSQL_VERSION_ID >= 32349)
	/*
	 * MySQL 3.23.49 disables LOAD DATA LOCAL by default. Use
	 * mysql_local_infile=1 in the DSN to enable it.
	 */
     if ((svp = hv_fetch( hv, "mysql_local_infile", 18, FALSE))  &&  *svp)
     {
	  unsigned int flag = SvTRUE(*svp);
	  if (dbis->debug >= 2)
	    PerlIO_printf(DBILOGFP,
        "imp_dbh->mysql_dr_connect: Using" \
        " local infile %u.\n", flag);
	  mysql_options(sock, MYSQL_OPT_LOCAL_INFILE, (const char *) &flag);
	}
#endif
      }
    }
    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP, "imp_dbh->mysql_dr_connect: client_flags = %d\n",
		    client_flag);
    result = mysql_real_connect(sock, host, user, password, dbname,
				portNr, mysql_socket, client_flag);
    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP, "imp_dbh->mysql_dr_connect: <-");

    if (result)
    {
#if MYSQL_VERSION_ID >=SERVER_PREPARE_VERSION
      /* connection succeeded. */
      /* imp_dbh == NULL when mysql_dr_connect() is called from mysql.xs
         functions (_admin_internal(),_ListDBs()). */
      if (!(result->client_flag & CLIENT_PROTOCOL_41) && imp_dbh)
        imp_dbh->use_server_side_prepare = FALSE;
#endif

      /*
        we turn off Mysql's auto reconnect and handle re-connecting ourselves
        so that we can keep track of when this happens.
      */
      result->reconnect=0;
    }
    return result;
  }
}

/*
  safe_hv_fetch
*/
static char *safe_hv_fetch(HV *hv, const char *name, int name_length)
{
  SV** svp;
  STRLEN len;
  char *res= NULL;

  if ((svp= hv_fetch(hv, name, name_length, FALSE)))
  {
    res= SvPV(*svp, len);
    if (!len)
      res= NULL;
  }
  return res;
}

/*
 Frontend for mysql_dr_connect
*/
static int my_login(SV* dbh, imp_dbh_t *imp_dbh)
{
  SV* sv;
  HV* hv;
  char* dbname;
  char* host;
  char* port;
  char* user;
  char* password;
  char* mysql_socket;

#if TAKE_IMP_DATA_VERSION
  if (DBIc_has(imp_dbh, DBIcf_IMPSET))
  { /* eg from take_imp_data() */
    if (DBIc_has(imp_dbh, DBIcf_ACTIVE))
    {
      if (dbis->debug >= 2)
        PerlIO_printf(DBILOGFP, "my_login skip connect\n");
      /* tell our parent we've adopted an active child */
      ++DBIc_ACTIVE_KIDS(DBIc_PARENT_COM(imp_dbh));
      return TRUE;
    }
    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP,
                    "my_login IMPSET but not ACTIVE so connect not skipped\n");
  }
#endif

  sv = DBIc_IMP_DATA(imp_dbh);

  if (!sv  ||  !SvROK(sv))
    return FALSE;

  hv = (HV*) SvRV(sv);
  if (SvTYPE(hv) != SVt_PVHV)
    return FALSE;

  host=		safe_hv_fetch(hv, "host", 4);
  port=		safe_hv_fetch(hv, "port", 4);
  user=		safe_hv_fetch(hv, "user", 4);
  password=	safe_hv_fetch(hv, "password", 8);
  dbname=	safe_hv_fetch(hv, "database", 8);
  mysql_socket=	safe_hv_fetch(hv, "mysql_socket", 12);

  if (dbis->debug >= 2)
    PerlIO_printf(DBILOGFP,
		  "imp_dbh->my_login : dbname = %s, uid = %s, pwd = %s," \
		  "host = %s, port = %s\n",
		  dbname ? dbname : "NULL",
		  user ? user : "NULL",
		  password ? password : "NULL",
		  host ? host : "NULL",
		  port ? port : "NULL");

  return mysql_dr_connect(dbh, &imp_dbh->mysql, mysql_socket, host, port, user,
			  password, dbname, imp_dbh) ? TRUE : FALSE;
}


/**************************************************************************
 *
 *  Name:    dbd_db_login
 *
 *  Purpose: Called for connecting to a database and logging in.
 *
 *  Input:   dbh - database handle being initialized
 *           imp_dbh - drivers private database handle data
 *           dbname - the database we want to log into; may be like
 *               "dbname:host" or "dbname:host:port"
 *           user - user name to connect as
 *           password - passwort to connect with
 *
 *  Returns: TRUE for success, FALSE otherwise; do_error has already
 *           been called in the latter case
 *
 **************************************************************************/

int dbd_db_login(SV* dbh, imp_dbh_t* imp_dbh, char* dbname, char* user,
		 char* password) {
#ifdef dTHR
  dTHR;
#endif

  if (dbis->debug >= 2)
    PerlIO_printf(DBILOGFP,
		  "imp_dbh->connect: dsn = %s, uid = %s, pwd = %s\n",
		  dbname ? dbname : "NULL",
		  user ? user : "NULL",
		  password ? password : "NULL");

  imp_dbh->stats.auto_reconnects_ok= 0;
  imp_dbh->stats.auto_reconnects_failed= 0;
  imp_dbh->bind_type_guessing= FALSE;
  imp_dbh->has_transactions= TRUE;
 /* Safer we flip this to TRUE perl side if we detect a mod_perl env. */
  imp_dbh->auto_reconnect = FALSE;

  if (!my_login(dbh, imp_dbh))
  {
    do_error(dbh, mysql_errno(&imp_dbh->mysql),
	     mysql_error(&imp_dbh->mysql));
    return FALSE;
  }

    /*
     *  Tell DBI, that dbh->disconnect should be called for this handle
     */
    DBIc_ACTIVE_on(imp_dbh);

    /* Tell DBI, that dbh->destroy should be called for this handle */
    DBIc_on(imp_dbh, DBIcf_IMPSET);

    return TRUE;
}


/***************************************************************************
 *
 *  Name:    dbd_db_commit
 *           dbd_db_rollback
 *
 *  Purpose: You guess what they should do. mSQL doesn't support
 *           transactions, so we stub commit to return OK
 *           and rollback to return ERROR in any case.
 *
 *  Input:   dbh - database handle being commited or rolled back
 *           imp_dbh - drivers private database handle data
 *
 *  Returns: TRUE for success, FALSE otherwise; do_error has already
 *           been called in the latter case
 *
 **************************************************************************/

int
dbd_db_commit(SV* dbh, imp_dbh_t* imp_dbh)
{
  if (DBIc_has(imp_dbh, DBIcf_AutoCommit))
  {
    do_warn(dbh, TX_ERR_AUTOCOMMIT,
	    "Commmit ineffective while AutoCommit is on");
    return TRUE;
  }

  if (imp_dbh->has_transactions)
  {
#if MYSQL_VERSION_ID < SERVER_PREPARE_VERSION                 
    if (mysql_real_query(&imp_dbh->mysql, "COMMIT", 6))
#else
      if (mysql_commit(&imp_dbh->mysql))
#endif
      {
        do_error(dbh, mysql_errno(&imp_dbh->mysql),
                 mysql_error(&imp_dbh->mysql));
        return FALSE;
      }
  }
  else
    do_warn(dbh, JW_ERR_NOT_IMPLEMENTED,
            "Commmit ineffective while AutoCommit is on");
  return TRUE;
}

/*
 dbd_db_rollback
*/
int
dbd_db_rollback(SV* dbh, imp_dbh_t* imp_dbh) {
  /* croak, if not in AutoCommit mode */
  if (DBIc_has(imp_dbh, DBIcf_AutoCommit))
  {
    do_warn(dbh, TX_ERR_AUTOCOMMIT,
            "Rollback ineffective while AutoCommit is on");
    return FALSE;
  }

  if (imp_dbh->has_transactions)
  {
#if MYSQL_VERSION_ID < SERVER_PREPARE_VERSION
    if (mysql_real_query(&imp_dbh->mysql, "ROLLBACK", 8))
#else
      if (mysql_rollback(&imp_dbh->mysql))
#endif
      {
        do_error(dbh, mysql_errno(&imp_dbh->mysql),
                 mysql_error(&imp_dbh->mysql));
        return FALSE;
      }
  }
  else
    do_error(dbh, JW_ERR_NOT_IMPLEMENTED,
             "Rollback ineffective while AutoCommit is on");
  return TRUE;
}
/* }}} */

/* {{{ int dbd_db_disconnect(SV* dbh, imp_dbh_t* imp_dbh)
 ***************************************************************************
 *
 *  Name:    dbd_db_disconnect
 *
 *  Purpose: Disconnect a database handle from its database
 *
 *  Input:   dbh - database handle being disconnected
 *           imp_dbh - drivers private database handle data
 *
 *  Returns: TRUE for success, FALSE otherwise; do_error has already
 *           been called in the latter case
 *
 **************************************************************************/

int dbd_db_disconnect(SV* dbh, imp_dbh_t* imp_dbh)
{
#ifdef dTHR
    dTHR;
#endif

    /* We assume that disconnect will always work       */
    /* since most errors imply already disconnected.    */
    DBIc_ACTIVE_off(imp_dbh);
    if (dbis->debug >= 2)
        PerlIO_printf(DBILOGFP, "&imp_dbh->mysql: %lx\n",
		      (long) &imp_dbh->mysql);
    mysql_close(&imp_dbh->mysql );

    /* We don't free imp_dbh since a reference still exists    */
    /* The DESTROY method is the only one to 'free' memory.    */
    return TRUE;
}


/***************************************************************************
 *
 *  Name:    dbd_discon_all
 *
 *  Purpose: Disconnect all database handles at shutdown time
 *
 *  Input:   dbh - database handle being disconnected
 *           imp_dbh - drivers private database handle data
 *
 *  Returns: TRUE for success, FALSE otherwise; do_error has already
 *           been called in the latter case
 *
 **************************************************************************/

int dbd_discon_all (SV *drh, imp_drh_t *imp_drh) {
#if defined(dTHR)
    dTHR;
#endif

#if defined(DBD_MYSQL_EMBEDDED)
    if (imp_drh->embedded.state)
    {
      if (dbis->debug >= 2)
        PerlIO_printf(DBILOGFP, "Stop embedded server\n");

      mysql_server_end();
      if (imp_drh->embedded.groups)
      {
        (void) SvREFCNT_dec(imp_drh->embedded.groups);
        imp_drh->embedded.groups = NULL;
      }

      if (imp_drh->embedded.args)
      {
        (void) SvREFCNT_dec(imp_drh->embedded.args);
        imp_drh->embedded.args = NULL;
      }


    }
#endif

    /* The disconnect_all concept is flawed and needs more work */
    if (!dirty && !SvTRUE(perl_get_sv("DBI::PERL_ENDING",0))) {
	sv_setiv(DBIc_ERR(imp_drh), (IV)1);
	sv_setpv(DBIc_ERRSTR(imp_drh),
		(char*)"disconnect_all not implemented");
	DBIh_EVENT2(drh, ERROR_event,
		    DBIc_ERR(imp_drh), DBIc_ERRSTR(imp_drh));
	return FALSE;
    }
    perl_destruct_level = 0;
    return FALSE;
}


/****************************************************************************
 *
 *  Name:    dbd_db_destroy
 *
 *  Purpose: Our part of the dbh destructor
 *
 *  Input:   dbh - database handle being destroyed
 *           imp_dbh - drivers private database handle data
 *
 *  Returns: Nothing
 *
 **************************************************************************/

void dbd_db_destroy(SV* dbh, imp_dbh_t* imp_dbh) {

    /*
     *  Being on the safe side never hurts ...
     */
  if (DBIc_ACTIVE(imp_dbh))
  {
    if (imp_dbh->has_transactions)
    {
      if (!DBIc_has(imp_dbh, DBIcf_AutoCommit))
#if MYSQL_VERSION_ID < SERVER_PREPARE_VERSION
        if ( mysql_real_query(&imp_dbh->mysql, "ROLLBACK", 8))
#else
        if (mysql_rollback(&imp_dbh->mysql))
#endif
            do_error(dbh, TX_ERR_ROLLBACK,"ROLLBACK failed");
    }
    dbd_db_disconnect(dbh, imp_dbh);
  }

  /* Tell DBI, that dbh->destroy must no longer be called */
  DBIc_off(imp_dbh, DBIcf_IMPSET);
}

/* 
 ***************************************************************************
 *
 *  Name:    dbd_db_STORE_attrib
 *
 *  Purpose: Function for storing dbh attributes; we currently support
 *           just nothing. :-)
 *
 *  Input:   dbh - database handle being modified
 *           imp_dbh - drivers private database handle data
 *           keysv - the attribute name
 *           valuesv - the attribute value
 *
 *  Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/
int
dbd_db_STORE_attrib(
                    SV* dbh,
                    imp_dbh_t* imp_dbh,
                    SV* keysv,
                    SV* valuesv
                   )
{
  STRLEN kl;
  char *key = SvPV(keysv, kl);
  SV *cachesv = Nullsv;
  int cacheit = FALSE;
  bool bool_value = SvTRUE(valuesv);

  if (kl==10 && strEQ(key, "AutoCommit"))
  {
    if (imp_dbh->has_transactions)
    {
      int oldval = DBIc_has(imp_dbh,DBIcf_AutoCommit);

      if (bool_value == oldval)
        return TRUE;

#if MYSQL_VERSION_ID >=SERVER_PREPARE_VERSION                 
      if (mysql_autocommit(&imp_dbh->mysql, bool_value))
      {
        do_error(dbh, TX_ERR_AUTOCOMMIT,
                 bool_value ? "Turning on AutoCommit failed" : "Turning off AutoCommit failed");
        return FALSE;
      }
#else
      /* if setting AutoCommit on ... */
      if (bool_value)
      {
        /* Setting autocommit will do a commit of any pending statement */
        if (mysql_real_query(&imp_dbh->mysql, "SET AUTOCOMMIT=1", 16))
        {
          do_error(dbh, TX_ERR_AUTOCOMMIT, "Turning on AutoCommit failed");
          return FALSE;
        }
      }
      else
      {
        if (mysql_real_query(&imp_dbh->mysql, "SET AUTOCOMMIT=0", 16))
        {
          do_error(dbh, TX_ERR_AUTOCOMMIT, "Turning off AutoCommit failed");
          return FALSE;
        }
      }
#endif
      DBIc_set(imp_dbh, DBIcf_AutoCommit, bool_value);
    }
    else
    {
      /*
       *  We do support neither transactions nor "AutoCommit".
       *  But we stub it. :-)
     */
      if (!SvTRUE(valuesv))
      {
        do_error(dbh, JW_ERR_NOT_IMPLEMENTED,
                 "Transactions not supported by database");
        croak("Transactions not supported by database");
      }
    }
  }
  else if (kl == 16 && strEQ(key,"mysql_use_result"))
    imp_dbh->use_mysql_use_result = bool_value;
  else if (kl == 20 && strEQ(key,"mysql_auto_reconnect"))
    /*XXX: Does DBI handle the magic ? */
    imp_dbh->auto_reconnect = bool_value;
  else if (kl == 20 && strEQ(key, "mysql_server_prepare"))
    imp_dbh->use_server_side_prepare=SvTRUE(valuesv);

  else if (kl == 31 && strEQ(key,"mysql_unsafe_bind_type_guessing"))
	imp_dbh->bind_type_guessing = SvIV(valuesv);
  else
    return FALSE;				/* Unknown key */

  if (cacheit) /* cache value for later DBI 'quick' fetch? */
    hv_store((HV*)SvRV(dbh), key, kl, cachesv, 0);
  return TRUE;
}

/***************************************************************************
 *
 *  Name:    dbd_db_FETCH_attrib
 *
 *  Purpose: Function for fetching dbh attributes
 *
 *  Input:   dbh - database handle being queried
 *           imp_dbh - drivers private database handle data
 *           keysv - the attribute name
 *
 *  Returns: An SV*, if sucessfull; NULL otherwise
 *
 *  Notes:   Do not forget to call sv_2mortal in the former case!
 *
 **************************************************************************/
static SV*
my_ulonglong2str(my_ulonglong val)
{
  char buf[64];
  char *ptr = buf + sizeof(buf) - 1;

  if (val == 0)
    return newSVpv("0", 1);

  *ptr = '\0';
  while (val > 0)
  {
    *(--ptr) = ('0' + (val % 10));
    val = val / 10;
  }
  return newSVpv(ptr, (buf+ sizeof(buf) - 1) - ptr);
}

SV*
dbd_db_FETCH_attrib(
                    SV* dbh,
                    imp_dbh_t* imp_dbh,
                    SV* keysv
                   )
{
  STRLEN kl;
  char *key = SvPV(keysv, kl);
  char* fine_key = NULL;
  SV* result = NULL;

  switch (*key) {
    case 'A':
      if (strEQ(key, "AutoCommit"))
      {
        if (imp_dbh->has_transactions)
          return sv_2mortal(boolSV(DBIc_has(imp_dbh,DBIcf_AutoCommit)));
        /* Default */
        return &sv_yes;
      }
      break;
  }
  if (strncmp(key, "mysql_", 6) == 0) {
    fine_key = key;
    key = key+6;
    kl = kl-6;
  }

  /* MONTY:  Check if kl should not be used or used everywhere */
  switch(*key) {
  case 'a':
    if (kl == strlen("auto_reconnect") && strEQ(key, "auto_reconnect"))
      result= sv_2mortal(newSViv(imp_dbh->auto_reconnect));
    break;
  case 'u':
    if (kl == strlen("unsafe_bind_type_guessing") &&
        strEQ(key, "unsafe_bind_type_guessing"))
      result = sv_2mortal(newSViv(imp_dbh->bind_type_guessing));
    break;
  case 'e':
    if (strEQ(key, "errno"))
      result= sv_2mortal(newSViv((IV)mysql_errno(&imp_dbh->mysql)));
    else if ( strEQ(key, "error") || strEQ(key, "errmsg"))
    {
    /* Note that errmsg is obsolete, as of 2.09! */
      const char* msg = mysql_error(&imp_dbh->mysql);
      result= sv_2mortal(newSVpv(msg, strlen(msg)));
    }
    break;

  case 'd':
    if (strEQ(key, "dbd_stats"))
    {
      HV* hv = newHV();
      hv_store(
               hv,
               "auto_reconnects_ok",
               strlen("auto_reconnects_ok"),
               newSViv(imp_dbh->stats.auto_reconnects_ok),
               0
              );
      hv_store(
               hv,
               "auto_reconnects_failed",
               strlen("auto_reconnects_failed"),
               newSViv(imp_dbh->stats.auto_reconnects_failed),
               0
              );

      result= (newRV_noinc((SV*)hv));
    }

  case 'h':
    if (strEQ(key, "hostinfo"))
    {
      const char* hostinfo = mysql_get_host_info(&imp_dbh->mysql);
      result= hostinfo ?
        sv_2mortal(newSVpv(hostinfo, strlen(hostinfo))) : &sv_undef;
    }
    break;

  case 'i':
    if (strEQ(key, "info"))
    {
      const char* info = mysql_info(&imp_dbh->mysql);
      result= info ? sv_2mortal(newSVpv(info, strlen(info))) : &sv_undef;
    }
    else if (kl == 8  &&  strEQ(key, "insertid"))
      /* We cannot return an IV, because the insertid is a long. */
      result= sv_2mortal(my_ulonglong2str(mysql_insert_id(&imp_dbh->mysql)));
    break;

  case 'p':
    if (kl == 9  &&  strEQ(key, "protoinfo"))
      result= sv_2mortal(newSViv(mysql_get_proto_info(&imp_dbh->mysql)));
    break;

  case 's':
    if (kl == 10  &&  strEQ(key, "serverinfo"))
    {
      const char* serverinfo = mysql_get_server_info(&imp_dbh->mysql);
      result= serverinfo ?
        sv_2mortal(newSVpv(serverinfo, strlen(serverinfo))) : &sv_undef;
    }
    else if (strEQ(key, "sock"))
      result= sv_2mortal(newSViv((IV) &imp_dbh->mysql));
    else if (strEQ(key, "sockfd"))
      result= sv_2mortal(newSViv((IV) imp_dbh->mysql.net.fd));
    else if (strEQ(key, "stat"))
    {
      const char* stats = mysql_stat(&imp_dbh->mysql);
      result= stats ?
        sv_2mortal(newSVpv(stats, strlen(stats))) : &sv_undef;
    }
    else if (strEQ(key, "stats"))
    {
      /* Obsolete, as of 2.09 */
      const char* stats = mysql_stat(&imp_dbh->mysql);
      result= stats ?
        sv_2mortal(newSVpv(stats, strlen(stats))) : &sv_undef;
    }
    else if (kl == 14 && strEQ(key,"server_prepare"))
      result= sv_2mortal(newSViv((IV) imp_dbh->use_server_side_prepare));
    break;

  case 't':
    if (kl == 9  &&  strEQ(key, "thread_id"))
      result= sv_2mortal(newSViv(mysql_thread_id(&imp_dbh->mysql)));
    break;
  }

  if (result== NULL)
    return Nullsv;

  return result;
}


/* 
 **************************************************************************
 *
 *  Name:    dbd_st_prepare
 *
 *  Purpose: Called for preparing an SQL statement; our part of the
 *           statement handle constructor
 *
 *  Input:   sth - statement handle being initialized
 *           imp_sth - drivers private statement handle data
 *           statement - pointer to string with SQL statement
 *           attribs - statement attributes, currently not in use
 *
 *  Returns: TRUE for success, FALSE otherwise; do_error will
 *           be called in the latter case
 *
 **************************************************************************/
int
dbd_st_prepare(
  SV *sth,
  imp_sth_t *imp_sth,
  char *statement,
  SV *attribs)
{
  int i;
#if MYSQL_VERSION_ID >= SERVER_PREPARE_VERSION
  char *searchptr;
  int col_type;
  int limit_flag= 0;
  int statement_length= 0;
  imp_sth_phb_t *fbind;
  MYSQL_BIND *bind, *bind_end;
#endif

  SV **svp;
  D_imp_dbh_from_sth;

#if MYSQL_VERSION_ID >= SERVER_PREPARE_VERSION
  statement_length = strlen(statement);
  imp_sth->fetch_done= 0;
#else
  /* Count the number of parameters (driver, vs server-side) */
  DBIc_NUM_PARAMS(imp_sth) = count_params(statement);
#endif

  imp_sth->done_desc= 0;
  imp_sth->result= NULL;
  imp_sth->currow= 0;

 /* Set default value of 'mysql_use_result' attribute for sth from dbh */
  svp= DBD_ATTRIB_GET_SVP(attribs, "mysql_use_result", 16);
  imp_sth->use_mysql_use_result= svp ?
    SvTRUE(*svp) : imp_dbh->use_mysql_use_result;

#if MYSQL_VERSION_ID >= SERVER_PREPARE_VERSION
 /* Set default value of 'mysql_server_prepare' attribute for sth from dbh */
  svp= DBD_ATTRIB_GET_SVP(attribs, "mysql_server_prepare", 20);
  imp_sth->use_server_side_prepare = svp ?
    SvTRUE(*svp) : imp_dbh->use_server_side_prepare;

  if (imp_sth->use_server_side_prepare)
  {
    /* This block is for the situation where there is a LIMIT statement
    * with placeholders, which MySQL currently doesn't support, so we
    * need to just parse the values and build an SQL statement with the
    * limit values replacing the placeholders */
    for ( i = 0; i < statement_length - 1; i++)
    {
      searchptr= &statement[i];
      /* prepared statements for SHOW commands are not supported */
      if ( ((*searchptr == 's' || *searchptr == 'S') &&
            (!strncmp(searchptr+1,"how ", 4) ||
             !strncmp(searchptr+1, "HOW ", 4)) ))
        imp_sth->use_server_side_prepare = 0;
      /* if there is a 'limit' in the statement... */
      if (!limit_flag && ((*searchptr == 'l' || *searchptr == 'L') &&
           (!strncmp(searchptr+1, "imit ?",6) || 
            (!strncmp(searchptr+1, "IMIT ?",6)) )))
      {
        limit_flag= 1;
        i+= 6;
      }
      if( limit_flag)
      {
        /* ... and place holders after the limit flag is set... */
        if (statement[i] == '?')
        {
          /* ... then we do not want to try server side prepare (use emulation) */
          imp_sth->use_server_side_prepare = 0;
          break;
        }
      }
    }
  }
#endif

  for (i= 0; i < AV_ATTRIB_LAST; i++)
  {
    imp_sth->av_attr[i]= Nullav;
  }


#if MYSQL_VERSION_ID >= SERVER_PREPARE_VERSION

  if (imp_sth->use_server_side_prepare == 0)
  {
    /* Count the number of parameters, the same way mysql_param_count does for server side prepares */
    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP, "dbd_st_prepare calling count_params (counting params emulation)\n");

    DBIc_NUM_PARAMS(imp_sth)= count_params(statement);
  }

 /*
  *  Perform check for LISTFIELDS command
  *  and if we met it then mark as uncompatible with new 4.1 protocol
  *  i.e. we leave imp_sth->use_server_side_prepare=0 for this stmt
  *  and it will be executed later in mysql_st_internal_execute()
  *  TODO: I think we can replace LISTFIELDS with SHOW COLUMNS [LIKE ...]
  *        to remove this extension hack
  */

    /* this is a better way to do this */
  if ( (!strncmp(statement, "listfields ", 11) ||
        !strncmp(statement, "LISTFIELDS ", 11)) &&
        imp_sth->use_server_side_prepare)
  {
    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP,
                    "\"listfields\" Statement: %s\n setting \
                    use_server_side_prepare to 0\n", statement);

    imp_sth->use_server_side_prepare= 0;
  }

  if (imp_sth->use_server_side_prepare)
  {
    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP,
                    "-->> dbd_st_prepare prepared statement query: %s\n",
                    statement);
    /* do we really need this? If we do, we should return, not just continue */
    if (imp_sth->stmt)
      fprintf(stderr,
              "ERROR: Trying to prepare new stmt while we have \
              already not closed one \n");

    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP,
                    "dbd_st_prepare calling mysql_stmt_init\n");

    imp_sth->stmt= mysql_stmt_init(&imp_dbh->mysql);

    if (! imp_sth->stmt)
    {
      PerlIO_printf(DBILOGFP,
                    "ERROR: Unable to return MYSQL_STMT structure \
                    from mysql_stmt_init(): ERROR NO: %d ERROR MSG:%s\n",
                    mysql_errno(&imp_dbh->mysql),
                    mysql_error(&imp_dbh->mysql));
    }

    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP,
                    "dbd_st_prepare calling mysql_stmt_prepare \
                    using statement %s length %d\n",
                    statement, statement_length);

    if (mysql_stmt_prepare(imp_sth->stmt, statement, statement_length))
    {

      mysql_stmt_close(imp_sth->stmt);
      imp_sth->stmt= NULL;
      do_error(sth, mysql_errno(&imp_dbh->mysql), mysql_error(&imp_dbh->mysql));

      /* For commands that are not supported by server side prepared statement
         mechanism lets try to pass them through regular API */
      if (mysql_errno(&imp_dbh->mysql) == ER_UNSUPPORTED_PS)
        imp_sth->use_server_side_prepare= 0;
      else
        return FALSE;
    }
    else
    {
      if (dbis->debug >= 2)
        PerlIO_printf(DBILOGFP,
                     "dbd_st_prepare called mysql_stmt_prepare, with %d params",
                      DBIc_NUM_PARAMS(imp_sth));

      DBIc_NUM_PARAMS(imp_sth)= mysql_stmt_param_count(imp_sth->stmt);
      /* mysql_stmt_param_count */

      if (DBIc_NUM_PARAMS(imp_sth) > 0)
      {
        int has_statement_fields= imp_sth->stmt->fields != 0;
        /* Allocate memory for bind variables */
        imp_sth->bind=            alloc_bind(DBIc_NUM_PARAMS(imp_sth));
        imp_sth->fbind=           alloc_fbind(DBIc_NUM_PARAMS(imp_sth));
        imp_sth->has_been_bound=  0;

        /* Initialize ph variables with  NULL values */
        for (bind=      imp_sth->bind,
             fbind=     imp_sth->fbind,
             bind_end=  bind+DBIc_NUM_PARAMS(imp_sth);
             bind < bind_end ;
             bind++, fbind++ )
        {
          /*
            if this statement has a result set, field types will be
            correctly identified. If there is no result set, such as
            with an INSERT, fields will not be defined, and all buffer_type
            will default to MYSQL_TYPE_VAR_STRING
          */
          col_type= (has_statement_fields ?
                     imp_sth->stmt->fields[i].type : MYSQL_TYPE_STRING);

          bind->buffer_type=  mysql_to_perl_type(col_type);
          bind->buffer=       NULL;
          bind->length=       &(fbind->length);
          bind->is_null=      (char*) &(fbind->is_null);
          fbind->is_null=     1;
          fbind->length=      0;
        }
      }
    }
  }
#endif

  /* Allocate memory for parameters */
  imp_sth->params= alloc_param(DBIc_NUM_PARAMS(imp_sth));
  DBIc_IMPSET_on(imp_sth);

  return 1;
}
/**************************************************************************
 *
 *  Name:    mysql_st_internal_execute
 *
 *  Purpose: Internal version for executing a statement, called both from
 *           within the "do" and the "execute" method.
 *
 *  Inputs:  h - object handle, for storing error messages
 *           statement - query being executed
 *           attribs - statement attributes, currently ignored
 *           num_params - number of parameters being bound
 *           params - parameter array
 *           result - where to store results, if any
 *           svsock - socket connected to the database
 *
 **************************************************************************/


my_ulonglong mysql_st_internal_execute(
                          SV *h,
                          SV *statement,
                          SV *attribs,
                          int num_params,
                          imp_sth_ph_t *params,
                          MYSQL_RES **result,
                          MYSQL *svsock,
                          int use_mysql_use_result
                         )
{
  D_imp_sth(h);
  D_imp_dbh_from_sth;
  STRLEN slen;
  char *sbuf = SvPV(statement, slen);
  char *salloc = parse_params(svsock,
                              sbuf,
                              &slen,
                              params,
                              num_params,
                              imp_dbh->bind_type_guessing);
  char *table;
  my_ulonglong rows= 0;

  if (dbis->debug >= 2)
    PerlIO_printf(DBILOGFP, "mysql_st_internal_execute\n");

  if (salloc)
  {
    sbuf= salloc;
    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP, "Binding parameters: %s\n", sbuf);
  }

  if (*result)
  {
    mysql_free_result(*result);
    *result= NULL;
  }

  if (slen >= 11 && (!strncmp(sbuf, "listfields ", 11) ||
                     !strncmp(sbuf, "LISTFIELDS ", 11)))
  {
    /* remove pre-space */
    slen-= 10;
    sbuf+= 10;
    while (slen && isspace(*sbuf)) { --slen;  ++sbuf; }

    if (!slen)
    {
      do_error(h, JW_ERR_QUERY, "Missing table name");
      return -2;
    }
    if (!(table= malloc(slen+1)))
    {
      do_error(h, JW_ERR_MEM, "Out of memory");
      return -2;
    }

    strncpy(table, sbuf, slen);
    sbuf= table;

    while (slen && !isspace(*sbuf))
    {
      --slen;
      ++sbuf;
    }
    *sbuf++= '\0';

    *result= mysql_list_fields(svsock, table, NULL);
    free(table);

    if (!(*result))
    {
      do_error(h, mysql_errno(svsock), mysql_error(svsock));
      return -2;
    }

    return 0;
  }

  if ((mysql_real_query(svsock, sbuf, slen))  &&
      (!mysql_db_reconnect(h)  ||
       (mysql_real_query(svsock, sbuf, slen))))
  {
    Safefree(salloc);
    do_error(h, mysql_errno(svsock), mysql_error(svsock));
    return -2;
  }
  Safefree(salloc);

  /** Store the result from the Query */
  *result= use_mysql_use_result ?
    mysql_use_result(svsock) : mysql_store_result(svsock);

  if (mysql_errno(svsock))
    do_error(h, mysql_errno(svsock), mysql_error(svsock));

  if (!*result)
    rows= mysql_affected_rows(svsock);
  else
    rows= mysql_num_rows(*result);

  return(rows);
}

 /**************************************************************************
 *
 *  Name:    mysql_st_internal_execute41
 *
 *  Purpose: Internal version for executing a prepared statement, called both
 *           from within the "do" and the "execute" method.
 *           MYSQL 4.1 API          
 *
 *
 *  Inputs:  h - object handle, for storing error messages
 *           statement - query being executed
 *           attribs - statement attributes, currently ignored
 *           num_params - number of parameters being bound
 *           params - parameter array
 *           result - where to store results, if any
 *           svsock - socket connected to the database
 *
 **************************************************************************/

#if MYSQL_VERSION_ID >= SERVER_PREPARE_VERSION

my_ulonglong mysql_st_internal_execute41(
                                         SV *h,
                                         SV *statement,
                                         SV *attribs,
                                         int num_params,
                                         imp_sth_ph_t *params,
                                         MYSQL_RES **result,
                                         MYSQL *svsock,
                                         int use_mysql_use_result,
                                         MYSQL_STMT *stmt,
                                         MYSQL_BIND *bind,
                                         int *has_been_bound
                                        )
{
  my_ulonglong rows;

  if (*result) /* do we free metadata info */
  {
    mysql_free_result(*result); /* free it if not */
    *result= NULL;
  }

  /*
    If were performed any changes with ph variables
    we have to rebind them
  */

  if (num_params > 0 && !(*has_been_bound))
  {

    if (mysql_stmt_bind_param(stmt,bind))
    {
      do_error(h, mysql_stmt_errno(stmt), mysql_stmt_error(stmt));
      return -2;
    }
    *has_been_bound= 1;
  }

  if (dbis->debug >= 2)
  {
    PerlIO_printf(DBILOGFP,
                  "mysql_st_internal_execute41 calling mysql_execute\n");
  }

  if (mysql_stmt_execute(stmt))
  {
    do_error(h, mysql_stmt_errno(stmt), mysql_stmt_error(stmt));
    return  -2;
  }

  /*
   This statement does not return a result set (INSERT, UPDATE...)
  */
  if (!(*result= mysql_stmt_result_metadata(stmt)))
  {
    if (mysql_stmt_errno(stmt))
    {
      do_error(h, mysql_stmt_errno(stmt), mysql_stmt_error(stmt));
      return -2;
    }
  }
  /*
    This statement returns a result set (SELECT...)
  */
  else
  {
      if (use_mysql_use_result)
        rows= mysql_num_rows(*result);
      else
      {
        /*
        * Get the total rows affected and return
        */
        if (mysql_stmt_store_result(stmt))
        {
          do_error(h, mysql_stmt_errno(stmt), mysql_stmt_error(stmt));
          return -2;
        }
        else
         rows= mysql_stmt_num_rows(stmt);
      }
  }
  return(rows);

}
#endif


/***************************************************************************
 *
 *  Name:    dbd_st_execute
 *
 *  Purpose: Called for preparing an SQL statement; our part of the
 *           statement handle constructor
 *
 *  Input:   sth - statement handle being initialized
 *           imp_sth - drivers private statement handle data
 *
 *  Returns: TRUE for success, FALSE otherwise; do_error will
 *           be called in the latter case
 *
 **************************************************************************/

int dbd_st_execute(SV* sth, imp_sth_t* imp_sth)
{
  char actual_row_num[64];
  int i;
  SV **statement;
  D_imp_dbh_from_sth;
#if defined (dTHR)
  dTHR;
#endif

  if (dbis->debug >= 2)
    PerlIO_printf(DBILOGFP,
      "    -> dbd_st_execute for %08lx\n", (u_long) sth);

  if (!SvROK(sth)  ||  SvTYPE(SvRV(sth)) != SVt_PVHV)
    croak("Expected hash array");

  /* Free cached array attributes */
  for (i= 0;  i < AV_ATTRIB_LAST;  i++)
  {
    if (imp_sth->av_attr[i])
      SvREFCNT_dec(imp_sth->av_attr[i]);

    imp_sth->av_attr[i]= Nullav;
  }

  

  statement= hv_fetch((HV*) SvRV(sth), "Statement", 9, FALSE);

#if MYSQL_VERSION_ID >= SERVER_PREPARE_VERSION

  if (imp_sth->use_server_side_prepare)
  {
   /* FIXME: Have to add do_error HERE */
    if (DBIc_ACTIVE(imp_sth) && !(mysql_st_clean_cursor(sth, imp_sth)))
      return 0;

    imp_sth->row_num= mysql_st_internal_execute41(
                                                  sth,
                                                  *statement,
                                                  NULL,
                                                  DBIc_NUM_PARAMS(imp_sth),
                                                  imp_sth->params,
                                                  &imp_sth->result,
                                                  &imp_dbh->mysql,
                                                  imp_sth->use_mysql_use_result,
                                                  imp_sth->stmt,
                                                  imp_sth->bind,
                                                  &imp_sth->has_been_bound
                                                 );
  }
  else
#endif
    imp_sth->row_num= mysql_st_internal_execute(
                                                sth,
                                                *statement,
                                                NULL,
                                                DBIc_NUM_PARAMS(imp_sth),
                                                imp_sth->params,
                                                &imp_sth->result,
                                                &imp_dbh->mysql,
                                                imp_sth->use_mysql_use_result
                                               );

  if (imp_sth->row_num+1 != (my_ulonglong)-1 )
  {
    if (!imp_sth->result)
      imp_sth->insertid= mysql_insert_id(&imp_dbh->mysql);
    else
    {
      /** Store the result in the current statement handle */
      DBIc_ACTIVE_on(imp_sth);
	    DBIc_NUM_FIELDS(imp_sth)= mysql_num_fields(imp_sth->result);
            imp_sth->done_desc= 0;
            imp_sth->fetch_done= 0;
    }
  }

  if (dbis->debug >= 2)
  {
    /* 
      PerlIO_printf doesn't always handle imp_sth->row_num %llu 
      consistantly!!
    */
    sprintf(actual_row_num, "%llu", imp_sth->row_num);
    PerlIO_printf(DBILOGFP,
                  "    <- dbd_st_execute returning imp_sth->row_num %s\n",
                  actual_row_num);
  }

  return (int)imp_sth->row_num;
}

 /**************************************************************************
 *
 *  Name:    dbd_describe
 *
 *  Purpose: Called from within the fetch method to describe the result
 *
 *  Input:   sth - statement handle being initialized
 *           imp_sth - our part of the statement handle, there's no
 *               need for supplying both; Tim just doesn't remove it
 *
 *  Returns: TRUE for success, FALSE otherwise; do_error will
 *           be called in the latter case
 *
 **************************************************************************/

int dbd_describe(SV* sth, imp_sth_t* imp_sth)
{

  if (dbis->debug >= 2)
    PerlIO_printf(DBILOGFP, "** dbd_describe() **\n");

#if MYSQL_VERSION_ID >= SERVER_PREPARE_VERSION

  if (imp_sth->use_server_side_prepare)
  {
    int i;
    int col_type;
    int num_fields= DBIc_NUM_FIELDS(imp_sth);
    imp_sth_fbh_t *fbh;
    MYSQL_BIND *bind;
    MYSQL_FIELD *fields;

    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP, "** dbd_describe() num_fields %d**\n", num_fields);

    if (imp_sth->done_desc)
      return TRUE;

    if (!num_fields || !imp_sth->result)
    {
      /* no metadata */
      do_error(sth, JW_ERR_SEQUENCE,
               "no metadata information while trying describe result set");
      return 0;
    }

    /* allocate fields buffers  */ 
    if (  !(imp_sth->fbh= alloc_fbuffer(num_fields))
          || !(imp_sth->buffer= alloc_bind(num_fields)) )
    {
      /* Out of memory */
      do_error(sth, JW_ERR_SEQUENCE, "Out of memory in dbd_sescribe()");
      return 0;
    }

    fields= mysql_fetch_fields(imp_sth->result);

    for (
         fbh= imp_sth->fbh, bind= (MYSQL_BIND*)imp_sth->buffer, i= 0;
         i < num_fields;
         i++, fbh++, bind++
        )
    {
      /* get the column type */
      col_type = fields ? fields[i].type : MYSQL_TYPE_STRING;
      if (dbis->debug >= 2)
        PerlIO_printf(DBILOGFP,
                      "col %d\ncol type %d\ncol len%d\ncol buf_len%d\n",
                      i, col_type, fbh->length, fields[i].length);

      bind->buffer_type= mysql_to_perl_type(col_type);
      bind->buffer_length= fields[i].length;
      bind->length= &(fbh->length);
      bind->is_null= &(fbh->is_null);
      Newz(908, fbh->data, fields[i].length, char);

      switch (bind->buffer_type) {
      case MYSQL_TYPE_DOUBLE:
        bind->buffer= (char*) &fbh->ddata;
        break;

      case MYSQL_TYPE_LONG:
        bind->buffer= (char*) &fbh->ldata;
        break;

      case MYSQL_TYPE_STRING:
        bind->buffer= (char *) fbh->data;

      default:
        bind->buffer= (char *) fbh->data;

      }
    }

    if (mysql_stmt_bind_result(imp_sth->stmt, imp_sth->buffer))
    {
      do_error(sth, mysql_stmt_errno(imp_sth->stmt),
               mysql_stmt_error(imp_sth->stmt));
      return 0;
    }
  }
#endif

  imp_sth->done_desc= 1;
  return TRUE;
}

/**************************************************************************
 *
 *  Name:    dbd_st_fetch
 *
 *  Purpose: Called for fetching a result row
 *
 *  Input:   sth - statement handle being initialized
 *           imp_sth - drivers private statement handle data
 *
 *  Returns: array of columns; the array is allocated by DBI via
 *           DBIS->get_fbav(imp_sth), even the values of the array
 *           are prepared, we just need to modify them appropriately
 *
 **************************************************************************/

AV*
dbd_st_fetch(SV *sth, imp_sth_t* imp_sth)
{
  int rc;
  int num_fields;
  int ChopBlanks;
  unsigned int i;
  unsigned long *lengths;
  AV *av;
  MYSQL_ROW cols;
  imp_sth_fbh_t *fbh;

#if MYSQL_VERSION_ID >=SERVER_PREPARE_VERSION
  MYSQL_BIND *bind;
#endif
  D_imp_dbh_from_sth;

#if MYSQL_VERSION_ID >=SERVER_PREPARE_VERSION
  if (imp_sth->use_server_side_prepare)
  {
    if (!DBIc_ACTIVE(imp_sth) )
    {
      do_error(sth, JW_ERR_SEQUENCE, "no statement executing\n");
      return Nullav;
    }

    if (imp_sth->fetch_done)
    {
      do_error(sth, JW_ERR_SEQUENCE, "fetch() but fetch already done");
      return Nullav;
    }

    if (!imp_sth->done_desc)
    {
      if (!dbd_describe(sth, imp_sth))
      {
        do_error(sth, JW_ERR_SEQUENCE, "Error while describe result set.");
        return Nullav;
      }
    }
  }
#endif

  ChopBlanks = DBIc_is(imp_sth, DBIcf_ChopBlanks);

  if (dbis->debug >= 2)
  {
    PerlIO_printf(DBILOGFP,
                  "    -> dbd_st_fetch for %08lx, chopblanks %d\n",
                  (u_long) sth, ChopBlanks);
  }

  if (!imp_sth->result)
  {
    do_error(sth, JW_ERR_SEQUENCE, "fetch() without execute()");
    return Nullav;
  }

  /* fix from 2.9008 */
  (imp_dbh->mysql).net.last_errno = 0;

#if MYSQL_VERSION_ID >=SERVER_PREPARE_VERSION
  if (imp_sth->use_server_side_prepare)
  {
    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP, "dbd_st_fetch calling mysql_fetch\n");

    if ((rc= mysql_stmt_fetch(imp_sth->stmt)))
    {
      if (rc == 1)
        do_error(sth, mysql_stmt_errno(imp_sth->stmt),
                 mysql_stmt_error(imp_sth->stmt));

      if (rc == 100)
      {
        /* Update row_num to affected_rows value */
        imp_sth->row_num= mysql_stmt_affected_rows(imp_sth->stmt);
        imp_sth->fetch_done=1;
      }

      if (!DBIc_COMPAT(imp_sth))
        dbd_st_finish(sth, imp_sth);

      return Nullav;
    }

    imp_sth->currow++;

    av= DBIS->get_fbav(imp_sth);
    num_fields= av_len(av)+1;
    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP,
                    "dbd_st_fetch called mysql_fetch, rc %d num_fields %d\n",
                    rc, num_fields);

    for (
         bind= imp_sth->buffer,
         fbh= imp_sth->fbh,
         i= 0;
         i < num_fields;
         i++,
         fbh++,
         bind++
        )
    {
      SV *sv= AvARRAY(av)[i]; /* Note: we (re)use the SV in the AV	*/

      /* This is wrong, null is not being set correctly
       * This is not the way to determine length (this would break blobs!)
       */
      if (fbh->is_null)
        (void) SvOK_off(sv);  /*  Field is NULL, return undef  */
      else
      {
        /* In case of BLOB/TEXT fields we allocate only 8192 bytes
           in dbd_describe() for data. Here we know real size of field
           so we should increase buffer size and refetch column value
        */
        if (fbh->length > bind->buffer_length)
        {
          if (dbis->debug >= 2)
            PerlIO_printf(DBILOGFP,"Refetch BLOB/TEXT column: %d\n", i);

          Renew(fbh->data, fbh->length, char);
          bind->buffer_length= fbh->length;
          bind->buffer= (char *) fbh->data;
          /*TODO: Use offset instead of 0 to fetch only remain part of data*/
          if (mysql_stmt_fetch_column(imp_sth->stmt, bind , i, 0))
            do_error(sth, mysql_stmt_errno(imp_sth->stmt),
                     mysql_stmt_error(imp_sth->stmt));
        }

        /* This does look a lot like Georg's PHP driver doesn't it?  --Brian */
        /* Credit due to Georg - mysqli_api.c  ;) --PMG */
        switch (bind->buffer_type) {
        case MYSQL_TYPE_DOUBLE:
          if (dbis->debug >= 2)
            PerlIO_printf(DBILOGFP, "st_fetch double data %f\n", fbh->ddata);
          sv_setnv(sv, fbh->ddata);
          break;

        case MYSQL_TYPE_LONG:
          if (dbis->debug >= 2)
            PerlIO_printf(DBILOGFP, "st_fetch int data %d\n", fbh->ldata);
          sv_setuv(sv, fbh->ldata);
          break;

        case MYSQL_TYPE_STRING:
          if (dbis->debug >= 2)
            PerlIO_printf(DBILOGFP, "st_fetch string data %s\n", fbh->data);
          sv_setpvn(sv, fbh->data, fbh->length);
          break;

        default:
          if (dbis->debug >= 2)
            PerlIO_printf(DBILOGFP, "ERROR IN st_fetch_string");
          sv_setpvn(sv, fbh->data, fbh->length);
          break;

        }
      }
    }

    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP, "<- dbd_st_fetch, %d cols\n", num_fields);

    return av;
  }
  else
  {
#endif

    imp_sth->currow++;

    if (!(cols = mysql_fetch_row(imp_sth->result)))
    {
      if (mysql_errno(&imp_dbh->mysql))
        do_error(sth, mysql_errno(&imp_dbh->mysql),
                 mysql_error(&imp_dbh->mysql));

      if (!DBIc_COMPAT(imp_sth))
        dbd_st_finish(sth, imp_sth);
      return Nullav;
    }

    lengths = mysql_fetch_lengths(imp_sth->result);

    av = DBIS->get_fbav(imp_sth);
    num_fields = av_len(av)+1;

    for (i= 0;  i < num_fields; ++i)
    {
      char *col= cols[i];
      SV *sv= AvARRAY(av)[i]; /* Note: we (re)use the SV in the AV	*/

      if (col)
      {
        STRLEN len= lengths[i];
        if (ChopBlanks)
        {
          while (len && col[len-1] == ' ')
          {	--len; }
        }
        sv_setpvn(sv, col, len);
      }
      else
        (void) SvOK_off(sv);  /*  Field is NULL, return undef  */
    }

    if (dbis->debug >= 2)
      PerlIO_printf(DBILOGFP, "    <- dbd_st_fetch, %d cols\n", num_fields);
    return av;

#if MYSQL_VERSION_ID  >= SERVER_PREPARE_VERSION
  }
#endif

}

#if MYSQL_VERSION_ID >= SERVER_PREPARE_VERSION
/*
  We have to fetch all data from stmt
  There is may be usefull for 2 cases:
  1. st_finish when we have undef statement
  2. call st_execute again when we have some unfetched data in stmt
 */

int mysql_st_clean_cursor(SV* sth, imp_sth_t* imp_sth) {

  if (DBIc_ACTIVE(imp_sth) && dbd_describe(sth, imp_sth) && !imp_sth->fetch_done)
    mysql_stmt_free_result(imp_sth->stmt);
  return 1;
}
#endif

/***************************************************************************
 *
 *  Name:    dbd_st_finish
 *
 *  Purpose: Called for freeing a mysql result
 *
 *  Input:   sth - statement handle being finished
 *           imp_sth - drivers private statement handle data
 *
 *  Returns: TRUE for success, FALSE otherwise; do_error() will
 *           be called in the latter case
 *
 **************************************************************************/

int dbd_st_finish(SV* sth, imp_sth_t* imp_sth) {

#if MYSQL_VERSION_ID >=SERVER_PREPARE_VERSION
  int i, num_fields;
  imp_sth_fbh_t *fbh;
#endif

#if defined (dTHR)
  dTHR;
#endif

#if MYSQL_VERSION_ID >= SERVER_PREPARE_VERSION
  if (imp_sth->use_server_side_prepare)
  {
    if (imp_sth && imp_sth->stmt)
    {
      if (!mysql_st_clean_cursor(sth, imp_sth))
      {
        do_error(sth, JW_ERR_SEQUENCE,
                 "Error happened while tried to clean up stmt");
        return 0;
      }

      if (imp_sth->fbh)
      {
        num_fields= DBIc_NUM_FIELDS(imp_sth);

        for (fbh= imp_sth->fbh, i= 0; i < num_fields; i++, fbh++)
        {
          if (fbh->data)
            Safefree(fbh->data);
        }
        FreeFBuffer(imp_sth->fbh);
      }
      FreeBind(imp_sth->buffer);

      imp_sth->buffer= NULL;
      imp_sth->fbh= NULL;
    }
  }
#endif

  /* 
    Cancel further fetches from this cursor.
    We don't close the cursor till DESTROY.
    The application may re execute it.
  */
  if (imp_sth && imp_sth->result)
  {
    mysql_free_result(imp_sth->result);
    imp_sth->result= NULL;
  }
  DBIc_ACTIVE_off(imp_sth);
  return 1;
}


/**************************************************************************
 *
 *  Name:    dbd_st_destroy
 *
 *  Purpose: Our part of the statement handles destructor
 *
 *  Input:   sth - statement handle being destroyed
 *           imp_sth - drivers private statement handle data
 *
 *  Returns: Nothing
 *
 **************************************************************************/

void dbd_st_destroy(SV *sth, imp_sth_t *imp_sth) {
  int i;

#if MYSQL_VERSION_ID >= SERVER_PREPARE_VERSION
  int num_fields;

  if (imp_sth->use_server_side_prepare)
  {
    if (imp_sth->stmt)
    {
      num_fields= DBIc_NUM_FIELDS(imp_sth);

      if (mysql_stmt_close(imp_sth->stmt))
      {
        PerlIO_printf(DBILOGFP,
                      "DESTROY: Error %s while close stmt\n",
                      (char *) mysql_stmt_error(imp_sth->stmt));
        do_error(sth, mysql_stmt_errno(imp_sth->stmt),mysql_stmt_error(imp_sth->stmt));
      }

      if (DBIc_NUM_PARAMS(imp_sth) > 0)
      {
        FreeBind(imp_sth->bind);
        FreeFBind(imp_sth->fbind);
      }

      imp_sth->bind= NULL;
      imp_sth->fbind= NULL;
    }
  }
#endif

  /* dbd_st_finish has already been called by .xs code if needed.	*/

  /* Free values allocated by dbd_bind_ph */
  FreeParam(imp_sth->params, DBIc_NUM_PARAMS(imp_sth));
  imp_sth->params= NULL;

  if (imp_sth->params)
  {
    FreeParam(imp_sth->params, DBIc_NUM_PARAMS(imp_sth));
    imp_sth->params= NULL;
  }

  /* Free cached array attributes */
  for (i= 0;  i < AV_ATTRIB_LAST;  i++)
  {
    if (imp_sth->av_attr[i])
      SvREFCNT_dec(imp_sth->av_attr[i]);
    imp_sth->av_attr[i]= Nullav;
  }
  /* let DBI know we've done it   */
  DBIc_IMPSET_off(imp_sth);
}


/*
 **************************************************************************
 *
 *  Name:    dbd_st_STORE_attrib
 *
 *  Purpose: Modifies a statement handles attributes; we currently
 *           support just nothing
 *
 *  Input:   sth - statement handle being destroyed
 *           imp_sth - drivers private statement handle data
 *           keysv - attribute name
 *           valuesv - attribute value
 *
 *  Returns: TRUE for success, FALSE otrherwise; do_error will
 *           be called in the latter case
 *
 **************************************************************************/
int
dbd_st_STORE_attrib(
                    SV *sth,
                    imp_sth_t *imp_sth,
                    SV *keysv,
                    SV *valuesv
                   )
{
  STRLEN(kl);
  char *key= SvPV(keysv, kl);
  int retval= FALSE;

  if (dbis->debug >= 2)
  {
    PerlIO_printf(DBILOGFP,
                  "-> dbd_st_STORE_attrib for %08lx, key %s\n",
                  (u_long) sth, key);
  }

  if (strEQ(key, "mysql_use_result"))
  {
    imp_sth->use_mysql_use_result= SvTRUE(valuesv);
  }

  if (dbis->debug >= 2)
  {
    PerlIO_printf(DBILOGFP,
                  "<- dbd_st_STORE_attrib for %08lx, result %d\n",
                  (u_long) sth, retval);
  }

  return retval;
}


/*
 **************************************************************************
 *
 *  Name:    dbd_st_FETCH_internal
 *
 *  Purpose: Retrieves a statement handles array attributes; we use
 *           a separate function, because creating the array
 *           attributes shares much code and it aids in supporting
 *           enhanced features like caching.
 *
 *  Input:   sth - statement handle; may even be a database handle,
 *               in which case this will be used for storing error
 *               messages only. This is only valid, if cacheit (the
 *               last argument) is set to TRUE.
 *           what - internal attribute number
 *           res - pointer to a DBMS result
 *           cacheit - TRUE, if results may be cached in the sth.
 *
 *  Returns: RV pointing to result array in case of success, NULL
 *           otherwise; do_error has already been called in the latter
 *           case.
 *
 **************************************************************************/

#ifndef IS_KEY
#define IS_KEY(A) (((A) & (PRI_KEY_FLAG | UNIQUE_KEY_FLAG | MULTIPLE_KEY_FLAG)) != 0)
#endif

#if !defined(IS_AUTO_INCREMENT) && defined(AUTO_INCREMENT_FLAG)
#define IS_AUTO_INCREMENT(A) (((A) & AUTO_INCREMENT_FLAG) != 0)
#endif

SV*
dbd_st_FETCH_internal(
  SV *sth,
  int what,
  MYSQL_RES *res,
  int cacheit
)
{
  D_imp_sth(sth);
  AV *av= Nullav;
  MYSQL_FIELD *curField;

  /* Are we asking for a legal value? */
  if (what < 0 ||  what >= AV_ATTRIB_LAST)
    do_error(sth, JW_ERR_NOT_IMPLEMENTED, "Not implemented");

  /* Return cached value, if possible */
  else if (cacheit  &&  imp_sth->av_attr[what])
    av= imp_sth->av_attr[what];

  /* Does this sth really have a result? */
  else if (!res)
    do_error(sth, JW_ERR_NOT_ACTIVE,
	     "statement contains no result");
  /* Do the real work. */
  else
  {
    av= newAV();
    mysql_field_seek(res, 0);
    while ((curField= mysql_fetch_field(res)))
    {
      SV *sv;

      switch(what) {
      case AV_ATTRIB_NAME:
        sv= newSVpv(curField->name, strlen(curField->name));
        break;

      case AV_ATTRIB_TABLE:
        sv= newSVpv(curField->table, strlen(curField->table));
        break;

      case AV_ATTRIB_TYPE:
        sv= newSViv((int) curField->type);
        break;

      case AV_ATTRIB_SQL_TYPE:
        sv= newSViv((int) native2sql(curField->type)->data_type);
        break;
      case AV_ATTRIB_IS_PRI_KEY:
        sv= boolSV(IS_PRI_KEY(curField->flags));
        break;

      case AV_ATTRIB_IS_NOT_NULL:
        sv= boolSV(IS_NOT_NULL(curField->flags));
        break;

      case AV_ATTRIB_NULLABLE:
        sv= boolSV(!IS_NOT_NULL(curField->flags));
        break;

      case AV_ATTRIB_LENGTH:
        sv= newSViv((int) curField->length);
        break;

      case AV_ATTRIB_IS_NUM:
        sv= newSViv((int) native2sql(curField->type)->is_num);
        break;

      case AV_ATTRIB_TYPE_NAME:
        sv= newSVpv((char*) native2sql(curField->type)->type_name, 0);
        break;

      case AV_ATTRIB_MAX_LENGTH:
        sv= newSViv((int) curField->max_length);
        break;

      case AV_ATTRIB_IS_AUTO_INCREMENT:
#if defined(AUTO_INCREMENT_FLAG)
        sv= boolSV(IS_AUTO_INCREMENT(curField->flags));
        break;
#else
        croak("AUTO_INCREMENT_FLAG is not supported on this machine");
#endif

      case AV_ATTRIB_IS_KEY:
        sv= boolSV(IS_KEY(curField->flags));
        break;

      case AV_ATTRIB_IS_BLOB:
        sv= boolSV(IS_BLOB(curField->flags));
        break;

      case AV_ATTRIB_SCALE:
        sv= newSViv((int) curField->decimals);
        break;

      case AV_ATTRIB_PRECISION:
        sv= newSViv((int) (curField->length > curField->max_length) ?
                     curField->length : curField->max_length);
        break;

      default:
        sv= &sv_undef;
        break;
      }
      av_push(av, sv);
    }

    /* Ensure that this value is kept, decremented in
     *  dbd_st_destroy and dbd_st_execute.  */
    if (!cacheit)
      return sv_2mortal(newRV_noinc((SV*)av));
    imp_sth->av_attr[what]= av;
  }

  if (av == Nullav)
    return &sv_undef;

  return sv_2mortal(newRV_inc((SV*)av));
}


/*
 **************************************************************************
 *
 *  Name:    dbd_st_FETCH_attrib
 *
 *  Purpose: Retrieves a statement handles attributes
 *
 *  Input:   sth - statement handle being destroyed
 *           imp_sth - drivers private statement handle data
 *           keysv - attribute name
 *
 *  Returns: NULL for an unknown attribute, "undef" for error,
 *           attribute value otherwise.
 *
 **************************************************************************/

#define ST_FETCH_AV(what) \
    dbd_st_FETCH_internal(sth, (what), imp_sth->result, TRUE)

  SV* dbd_st_FETCH_attrib(
                          SV *sth,
                          imp_sth_t *imp_sth,
                          SV *keysv
                         )
{
  STRLEN(kl);
  char *key= SvPV(keysv, kl);
  SV *retsv= Nullsv;
  if (kl < 2)
    return Nullsv;

  if (dbis->debug >= 2)
    PerlIO_printf(DBILOGFP,
                  "    -> dbd_st_FETCH_attrib for %08lx, key %s\n",
                  (u_long) sth, key);

  switch (*key) {
  case 'N':
    if (strEQ(key, "NAME"))
      retsv= ST_FETCH_AV(AV_ATTRIB_NAME);
    else if (strEQ(key, "NULLABLE"))
      retsv= ST_FETCH_AV(AV_ATTRIB_NULLABLE);
    break;
  case 'P':
    if (strEQ(key, "PRECISION"))
      retsv= ST_FETCH_AV(AV_ATTRIB_PRECISION);
    break;
  case 'S':
    if (strEQ(key, "SCALE"))
      retsv= ST_FETCH_AV(AV_ATTRIB_SCALE);
    break;
  case 'T':
    if (strEQ(key, "TYPE"))
      retsv= ST_FETCH_AV(AV_ATTRIB_SQL_TYPE);
    break;
  case 'm':
    switch (kl) {
    case 10:
      if (strEQ(key, "mysql_type"))
        retsv= ST_FETCH_AV(AV_ATTRIB_TYPE);
      break;
    case 11:
      if (strEQ(key, "mysql_table"))
        retsv= ST_FETCH_AV(AV_ATTRIB_TABLE);
      break;
    case 12:
      if (       strEQ(key, "mysql_is_key"))
        retsv= ST_FETCH_AV(AV_ATTRIB_IS_KEY);
      else if (strEQ(key, "mysql_is_num"))
        retsv= ST_FETCH_AV(AV_ATTRIB_IS_NUM);
      else if (strEQ(key, "mysql_length"))
        retsv= ST_FETCH_AV(AV_ATTRIB_LENGTH);
      else if (strEQ(key, "mysql_result"))
        retsv= sv_2mortal(newSViv((IV) imp_sth->result));
      break;
    case 13:
      if (strEQ(key, "mysql_is_blob"))
        retsv= ST_FETCH_AV(AV_ATTRIB_IS_BLOB);
      break;
    case 14:
      if (strEQ(key, "mysql_insertid"))
      {
        /* We cannot return an IV, because the insertid is a long.  */
        if (dbis->debug >= 2)
          PerlIO_printf(DBILOGFP, "INSERT ID %d\n", imp_sth->insertid);

        return sv_2mortal(my_ulonglong2str(imp_sth->insertid));
      }
      break;
    case 15:
      if (strEQ(key, "mysql_type_name"))
        retsv = ST_FETCH_AV(AV_ATTRIB_TYPE_NAME);
      break;
    case 16:
      if ( strEQ(key, "mysql_is_pri_key"))
        retsv= ST_FETCH_AV(AV_ATTRIB_IS_PRI_KEY);
      else if (strEQ(key, "mysql_max_length"))
        retsv= ST_FETCH_AV(AV_ATTRIB_MAX_LENGTH);
      else if (strEQ(key, "mysql_use_result"))
        retsv= boolSV(imp_sth->use_mysql_use_result);
      break;
    case 20:
      if (strEQ(key, "mysql_server_prepare"))
#if MYSQL_VERSION_ID >= SERVER_PREPARE_VERSION
        retsv= sv_2mortal(newSViv((IV) imp_sth->use_server_side_prepare));
#else
      retsv= boolSV(0);
#endif
      break;
    case 23:
      if (strEQ(key, "mysql_is_auto_increment"))
        retsv = ST_FETCH_AV(AV_ATTRIB_IS_AUTO_INCREMENT);
      break;
    }
    break;
  }
  return retsv;
}


/***************************************************************************
 *
 *  Name:    dbd_st_blob_read
 *
 *  Purpose: Used for blob reads if the statement handles "LongTruncOk"
 *           attribute (currently not supported by DBD::mysql)
 *
 *  Input:   SV* - statement handle from which a blob will be fetched
 *           imp_sth - drivers private statement handle data
 *           field - field number of the blob (note, that a row may
 *               contain more than one blob)
 *           offset - the offset of the field, where to start reading
 *           len - maximum number of bytes to read
 *           destrv - RV* that tells us where to store
 *           destoffset - destination offset
 *
 *  Returns: TRUE for success, FALSE otrherwise; do_error will
 *           be called in the latter case
 *
 **************************************************************************/

int dbd_st_blob_read (
  SV *sth,
  imp_sth_t *imp_sth,
  int field,
  long offset,
  long len,
  SV *destrv,
  long destoffset)
{
    return FALSE;
}


/***************************************************************************
 *
 *  Name:    dbd_bind_ph
 *
 *  Purpose: Binds a statement value to a parameter
 *
 *  Input:   sth - statement handle
 *           imp_sth - drivers private statement handle data
 *           param - parameter number, counting starts with 1
 *           value - value being inserted for parameter "param"
 *           sql_type - SQL type of the value
 *           attribs - bind parameter attributes, currently this must be
 *               one of the values SQL_CHAR, ...
 *           inout - TRUE, if parameter is an output variable (currently
 *               this is not supported)
 *           maxlen - ???
 *
 *  Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/

int dbd_bind_ph (SV *sth, imp_sth_t *imp_sth, SV *param, SV *value,
		 IV sql_type, SV *attribs, int is_inout, IV maxlen) {
  int rc;
  int param_num= SvIV(param);
  int idx= param_num - 1;   
  char err_msg[64];

#if MYSQL_VERSION_ID >=40101
  STRLEN slen;
  char *buffer;
  int buffer_is_null= 0;
  int buffer_length= slen;
  int buffer_type= 0;
#endif

  if (param_num <= 0  ||  param_num > DBIc_NUM_PARAMS(imp_sth))
  {
    do_error(sth, JW_ERR_ILLEGAL_PARAM_NUM,
             "Illegal parameter number");
    return FALSE;
  }

  /* 
     This fixes the bug whereby no warning was issued upone binding a 
     defined non-numeric as numeric
   */
  if (SvOK(value) &&
      (sql_type == SQL_NUMERIC  ||
       sql_type == SQL_DECIMAL  ||
       sql_type == SQL_INTEGER  ||
       sql_type == SQL_SMALLINT ||
       sql_type == SQL_FLOAT    ||
       sql_type == SQL_REAL     ||
       sql_type == SQL_DOUBLE) )
  {
    if (! looks_like_number(value))
    {
      sprintf(err_msg,
              "Binding non-numeric field %d, value %s as a numeric!",
              param_num, neatsvpv(value,0));
      do_error(sth, JW_ERR_ILLEGAL_PARAM_NUM, err_msg);
    }
  }

  if (is_inout)
  {
    do_error(sth, JW_ERR_NOT_IMPLEMENTED,
             "Output parameters not implemented");
    return FALSE;
  }

  rc = bind_param(&imp_sth->params[idx], value, sql_type);

#if MYSQL_VERSION_ID >= SERVER_PREPARE_VERSION
    if (imp_sth->use_server_side_prepare)
    {
      if (SvOK(imp_sth->params[idx].value) && imp_sth->params[idx].value)
      {
        buffer= SvPV(imp_sth->params[idx].value, slen);
        buffer_is_null= 0;
        buffer_length= slen;
      }
      else
      {
        buffer= NULL;
        buffer_is_null= 1;
        buffer_length= 0;
      }

      switch(sql_type) {
        case SQL_NUMERIC:
        case SQL_INTEGER:
        case SQL_SMALLINT:
        case SQL_BIGINT:
        case SQL_TINYINT:
          /* INT */
          buffer_type= MYSQL_TYPE_LONG;
          if (dbis->debug)
            PerlIO_printf(DBILOGFP, "   SCALAR type %d ->%s<- IS A INT NUMBER\n", sql_type, buffer);
          break;

        case SQL_DOUBLE:
        case SQL_DECIMAL:
        case SQL_FLOAT:
        case SQL_REAL:
          /* FLOAT */
          buffer_type= MYSQL_TYPE_DOUBLE;
          if (dbis->debug)
            PerlIO_printf(DBILOGFP, "   SCALAR type %d ->%s<- IS A FLOAT NUMBER\n", sql_type, buffer);
          break;

        case SQL_CHAR:
        case SQL_VARCHAR:
        case SQL_DATE:
        case SQL_TIME:
        case SQL_TIMESTAMP:
        case SQL_LONGVARCHAR:
        case SQL_BINARY:
        case SQL_VARBINARY:
        case SQL_LONGVARBINARY:
          buffer_type= MYSQL_TYPE_STRING;
          if (dbis->debug)
            PerlIO_printf(DBILOGFP, "   SCALAR type %d ->%s<- IS A STRING\n", sql_type, buffer);
          break;

        default:
          buffer_type= MYSQL_TYPE_STRING;
          if (dbis->debug)
            PerlIO_printf(DBILOGFP, "   SCALAR type %d ->%s<- IS A STRING\n", sql_type, buffer);
          break;
      }

      if (buffer_is_null)
        buffer_type= MYSQL_TYPE_NULL;

      /* prepare has not been called */
      if (imp_sth->has_been_bound == 0) 
      {
        imp_sth->bind[idx].buffer_type= buffer_type;
        imp_sth->bind[idx].buffer= buffer;
        imp_sth->bind[idx].buffer_length= buffer_length;
      }
      else /* prepare has been called */
      {
        imp_sth->stmt->params[idx].buffer_type= buffer_type;
        imp_sth->stmt->params[idx].buffer= buffer;
        imp_sth->stmt->params[idx].buffer_length= buffer_length;
      }
      imp_sth->fbind[idx].length= buffer_length;
      imp_sth->fbind[idx].is_null= buffer_is_null;
    }
#endif
    return rc;
}


/***************************************************************************
 *
 *  Name:    mysql_db_reconnect
 *
 *  Purpose: If the server has disconnected, try to reconnect.
 *
 *  Input:   h - database or statement handle
 *
 *  Returns: TRUE for success, FALSE otherwise
 *
 **************************************************************************/

int mysql_db_reconnect(SV* h)
{
  D_imp_xxh(h);
  imp_dbh_t* imp_dbh;
  MYSQL save_socket;

  if (DBIc_TYPE(imp_xxh) == DBIt_ST)
  {
    imp_dbh = (imp_dbh_t*) DBIc_PARENT_COM(imp_xxh);
    h = DBIc_PARENT_H(imp_xxh);
  }
  else
    imp_dbh= (imp_dbh_t*) imp_xxh;

  if (mysql_errno(&imp_dbh->mysql) != CR_SERVER_GONE_ERROR)
    /* Other error */
    return FALSE;

  if (!DBIc_has(imp_dbh, DBIcf_AutoCommit) || !imp_dbh->auto_reconnect)
  {
    /* We never reconnect if AutoCommit is turned off.
     * Otherwise we might get an inconsistent transaction
     * state.
     */
    return FALSE;
  }

  /* my_login will blow away imp_dbh->mysql so we save a copy of
   * imp_dbh->mysql and put it back where it belongs if the reconnect
   * fail.  Think server is down & reconnect fails but the application eval{}s
   * the execute, so next time $dbh->quote() gets called, instant SIGSEGV!
   */
  save_socket= imp_dbh->mysql;
  memcpy (&save_socket, &imp_dbh->mysql,sizeof(save_socket));
  memset (&imp_dbh->mysql,0,sizeof(imp_dbh->mysql));

  if (!my_login(h, imp_dbh))
  {
    do_error(h, mysql_errno(&imp_dbh->mysql), mysql_error(&imp_dbh->mysql));
    memcpy (&imp_dbh->mysql, &save_socket, sizeof(save_socket));
    ++imp_dbh->stats.auto_reconnects_failed;
    return FALSE;
  }
  ++imp_dbh->stats.auto_reconnects_ok;
  return TRUE;
}


/**************************************************************************
 *
 *  Name:    dbd_db_type_info_all
 *
 *  Purpose: Implements $dbh->type_info_all
 *
 *  Input:   dbh - database handle
 *           imp_sth - drivers private database handle data
 *
 *  Returns: RV to AV of types
 *
 **************************************************************************/

#define PV_PUSH(c)                              \
    if (c) {                                    \
	sv= newSVpv((char*) (c), 0);           \
	SvREADONLY_on(sv);                      \
    } else {                                    \
        sv= &sv_undef;                         \
    }                                           \
    av_push(row, sv);

#define IV_PUSH(i) sv= newSViv((i)); SvREADONLY_on(sv); av_push(row, sv);

AV *dbd_db_type_info_all(SV *dbh, imp_dbh_t *imp_dbh)
{
  AV *av= newAV();
  AV *row;
  HV *hv;
  SV *sv;
  int i;
  const char *cols[] = {
    "TYPE_NAME",
    "DATA_TYPE",
    "COLUMN_SIZE",
    "LITERAL_PREFIX",
    "LITERAL_SUFFIX",
    "CREATE_PARAMS",
    "NULLABLE",
    "CASE_SENSITIVE",
    "SEARCHABLE",
    "UNSIGNED_ATTRIBUTE",
    "FIXED_PREC_SCALE",
    "AUTO_UNIQUE_VALUE",
    "LOCAL_TYPE_NAME",
    "MINIMUM_SCALE",
    "MAXIMUM_SCALE",
    "NUM_PREC_RADIX",
    "SQL_DATATYPE",
    "SQL_DATETIME_SUB",
    "INTERVAL_PRECISION",
    "mysql_native_type",
    "mysql_is_num"
  };

  hv= newHV();
  av_push(av, newRV_noinc((SV*) hv));
  for (i= 0;  i < (int)(sizeof(cols) / sizeof(const char*));  i++)
  {
    if (!hv_store(hv, (char*) cols[i], strlen(cols[i]), newSViv(i), 0))
    {
      SvREFCNT_dec((SV*) av);
      return Nullav;
    }
  }
  for (i= 0;  i < (int)SQL_GET_TYPE_INFO_num;  i++)
  {
    const sql_type_info_t *t= &SQL_GET_TYPE_INFO_values[i];

    row= newAV();
    av_push(av, newRV_noinc((SV*) row));
    PV_PUSH(t->type_name);
    IV_PUSH(t->data_type);
    IV_PUSH(t->column_size);
    PV_PUSH(t->literal_prefix);
    PV_PUSH(t->literal_suffix);
    PV_PUSH(t->create_params);
    IV_PUSH(t->nullable);
    IV_PUSH(t->case_sensitive);
    IV_PUSH(t->searchable);
    IV_PUSH(t->unsigned_attribute);
    IV_PUSH(t->fixed_prec_scale);
    IV_PUSH(t->auto_unique_value);
    PV_PUSH(t->local_type_name);
    IV_PUSH(t->minimum_scale);
    IV_PUSH(t->maximum_scale);

    if (t->num_prec_radix)
    {
      IV_PUSH(t->num_prec_radix);
    }
    else
      av_push(row, &sv_undef);

    IV_PUSH(t->sql_datatype); /* SQL_DATATYPE*/
    IV_PUSH(t->sql_datetime_sub); /* SQL_DATETIME_SUB*/
    IV_PUSH(t->interval_precision); /* INTERVAL_PERCISION */
    IV_PUSH(t->native_type);
    IV_PUSH(t->is_num);
  }
  return av;
}


/*
  dbd_db_quote

  Properly quotes a value 
*/
SV* dbd_db_quote(SV *dbh, SV *str, SV *type)
{
  SV *result;

  if (SvGMAGICAL(str))
    mg_get(str);

  if (!SvOK(str))
    result= newSVpv("NULL", 4);
  else
  {
    char *ptr, *sptr;
    STRLEN len;

    D_imp_dbh(dbh);

    if (type  &&  SvOK(type))
    {
      int i;
      int tp= SvIV(type);
      for (i= 0;  i < (int)SQL_GET_TYPE_INFO_num;  i++)
      {
        const sql_type_info_t *t= &SQL_GET_TYPE_INFO_values[i];
        if (t->data_type == tp)
        {
          if (!t->literal_prefix)
            return Nullsv;
          break;
        }
      }
    }

    ptr= SvPV(str, len);
    result= newSV(len*2+3);
    sptr= SvPVX(result);

    *sptr++ = '\'';
    sptr+= mysql_real_escape_string(&imp_dbh->mysql, sptr,
                                     ptr, len);
    *sptr++= '\'';
    SvPOK_on(result);
    SvCUR_set(result, sptr - SvPVX(result));
    /* Never hurts NUL terminating a Per string */
    *sptr++= '\0';
  }
  return result;
}

#ifdef DBD_MYSQL_INSERT_ID_IS_GOOD
SV *mysql_db_last_insert_id(SV *dbh, imp_dbh_t *imp_dbh,
        SV *catalog, SV *schema, SV *table, SV *field, SV *attr)
{
  return sv_2mortal(my_ulonglong2str(mysql_insert_id(&((imp_dbh_t*)imp_dbh)->mysql)));
}
#endif
