/* Hej, Emacs, this is -*- C -*- mode!

   $Id: dbd.xs.in,v 1.4 1999/09/07 21:55:51 joe Exp $

   Copyright (c) 1997, 1998 Jochen Wiedmann

   You may distribute under the terms of either the GNU General Public
   License or the Artistic License, as specified in the Perl README file,
   with the exception that it cannot be placed on a CD-ROM or similar media
   for commercial distribution without the prior approval of the author.

*/


#include "dbdimp.h"
#include "constants.h"


DBISTATE_DECLARE;


MODULE = DBD::mysql	PACKAGE = DBD::mysql

INCLUDE: mysql.xsi

MODULE = DBD::mysql	PACKAGE = DBD::mysql

double
constant(name, arg)
    char* name
    char* arg
  CODE:
    RETVAL = mymsql_constant(name, arg);
  OUTPUT:
    RETVAL


MODULE = DBD::mysql	PACKAGE = DBD::mysql::dr

void
_ListDBs(drh, host, port=NULL, user=NULL, password=NULL)
    SV *        drh
    char *	host
    char *      port
    char *      user
    char *      password
  PPCODE:
{
    MYSQL mysql;
    MYSQL* sock = mysql_dr_connect(&mysql, NULL, host, port, user, password,
				   NULL, NULL);
    if (sock != NULL) {
      MYSQL_ROW cur;
      MYSQL_RES* res = mysql_list_dbs(sock, NULL);
      if (!res) {
	do_error(drh, mysql_errno(sock), mysql_error(sock));
      } else {
	EXTEND(sp, mysql_num_rows(res));
	while ((cur = mysql_fetch_row(res))) {
	  PUSHs(sv_2mortal((SV*)newSVpv(cur[0], strlen(cur[0]))));
	}
	mysql_free_result(res);
      }
      mysql_close(sock);
    }
}


void
_admin_internal(drh,dbh,command,dbname=NULL,host=NULL,port=NULL,user=NULL,password=NULL)
    SV* drh
    SV* dbh
    char* command
    char* dbname
    char* host
    char* port
    char* user
    char* password
  PPCODE:
    {
	MYSQL mysql;
	int result;
	MYSQL* sock;

	/*
	 *  Connect to the database, if required.
	 */
	if (SvOK(dbh)) {
	    D_imp_dbh(dbh);
	    sock = &imp_dbh->mysql;
	} else {
	  sock = mysql_dr_connect(&mysql, NULL, host, port, user,
				  password, NULL, NULL);
	  if (sock == NULL) {
	    do_error(drh, mysql_errno(&mysql), mysql_error(&mysql));
	    XSRETURN_NO;
	  }
       }
 
       if (strEQ(command, "shutdown")) {
	   result = mysql_shutdown(sock);
       } else if (strEQ(command, "reload")) {
	   result = mysql_reload(sock);
       } else if (strEQ(command, "createdb")) {
#if MYSQL_VERSION_ID < 40000
	   result = mysql_create_db(sock, dbname);
#else
	   char* buffer = malloc(strlen(dbname)+50);
	   if (buffer == NULL) {
	     do_error(drh, JW_ERR_MEM, "Out of memory");
	     XSRETURN_NO;
	   } else {
	     strcpy(buffer, "CREATE DATABASE ");
	     strcat(buffer, dbname);
	     result = mysql_real_query(sock, buffer, strlen(buffer));
	     free(buffer);
	   }
#endif
       } else if (strEQ(command, "dropdb")) {
#if MYSQL_VERSION_ID < 40000
          result = mysql_drop_db(sock, dbname);
#else
	   char* buffer = malloc(strlen(dbname)+50);
	   if (buffer == NULL) {
	     do_error(drh, JW_ERR_MEM, "Out of memory");
	     XSRETURN_NO;
	   } else {
	     strcpy(buffer, "DROP DATABASE ");
	     strcat(buffer, dbname);
	     result = mysql_real_query(sock, buffer, strlen(buffer));
	     free(buffer);
	   }
#endif
       } else {
	  croak("Unknown command: %s", command);
       }
       if (result) {
	 do_error(SvOK(dbh) ? dbh : drh, mysql_errno(sock),
		  mysql_error(sock));
       }
       if (SvOK(dbh)) {
	   mysql_close(sock);
       }
       if (result) { XSRETURN_NO; } else { XSRETURN_YES; }
   }


MODULE = DBD::mysql    PACKAGE = DBD::mysql::db


void
type_info_all(dbh)
    SV* dbh
  PPCODE:
    {
/* 	static AV* types = NULL; */
/* 	if (!types) { */
/* 	    D_imp_dbh(dbh); */
/* 	    if (!(types = dbd_db_type_info_all(dbh, imp_dbh))) { */
/* 	        croak("Cannot create types array (out of memory?)"); */
/* 	    } */
/* 	} */
/* 	ST(0) = sv_2mortal(newRV_inc((SV*) types)); */
        D_imp_dbh(dbh);
	ST(0) = sv_2mortal(newRV_noinc((SV*) dbd_db_type_info_all(dbh,
								  imp_dbh)));
	XSRETURN(1);
    }


void
_ListDBs(dbh)
    SV*	dbh
  PPCODE:
    D_imp_dbh(dbh);
    MYSQL_RES* res = mysql_list_dbs(&imp_dbh->mysql, NULL);
    MYSQL_ROW cur;
    if (!res  &&
	(!mysql_db_reconnect(dbh)  ||
	 !(res = mysql_list_dbs(&imp_dbh->mysql, NULL)))) {
      do_error(dbh, mysql_errno(&imp_dbh->mysql),
	       mysql_error(&imp_dbh->mysql));
    } else {
      EXTEND(sp, mysql_num_rows(res));
      while ((cur = mysql_fetch_row(res))) {
	PUSHs(sv_2mortal((SV*)newSVpv(cur[0], strlen(cur[0]))));
      }
      mysql_free_result(res);
    }
 

void
do(dbh, statement, attr=Nullsv, ...)
    SV *        dbh
    SV *	statement
    SV *        attr
  PROTOTYPE: $$;$@      
  CODE:
{
    D_imp_dbh(dbh);
    struct imp_sth_ph_st* params = NULL;
    int numParams = 0;
    MYSQL_RES* cda = NULL;
    int retval;

    if (items > 3) {
      /*  Handle binding supplied values to placeholders	     */
      /*  Assume user has passed the correct number of parameters  */
      int i;
      numParams = items-3;
      Newz(0, params, sizeof(*params)*numParams, struct imp_sth_ph_st);
      for (i = 0;  i < numParams;  i++) {
	params[i].value = ST(i+3);
	params[i].type = SQL_VARCHAR;
      }
    }
    retval = mysql_st_internal_execute(dbh, statement, attr, numParams,
				       params, &cda, &imp_dbh->mysql, 0);
    Safefree(params);
    if (cda) {
      mysql_free_result(cda);
    }
    /* remember that dbd_st_execute must return <= -2 for error	*/
    if (retval == 0)		/* ok with no rows affected	*/
	XST_mPV(0, "0E0");	/* (true but zero)		*/
    else if (retval < -1)	/* -1 == unknown number of rows	*/
	XST_mUNDEF(0);		/* <= -2 means error   		*/
    else
	XST_mIV(0, retval);	/* typically 1, rowcount or -1	*/
}


SV*
ping(dbh)
    SV* dbh;
  PROTOTYPE: $
  CODE:
    {
      int result;
      D_imp_dbh(dbh);
      result = (mysql_ping(&imp_dbh->mysql) == 0);
      if (!result) {
	if (mysql_db_reconnect(dbh)) {
	  result = (mysql_ping(&imp_dbh->mysql) == 0);
	}
      }
      RETVAL = boolSV(result);
    }
  OUTPUT:
    RETVAL



void
quote(dbh, str, type=NULL)
    SV* dbh
    SV* str
    SV* type
  PROTOTYPE: $$;$
  PPCODE:
    {
        SV* quoted = dbd_db_quote(dbh, str, type);
	ST(0) = quoted ? sv_2mortal(quoted) : str;
	XSRETURN(1);
    }


MODULE = DBD::mysql    PACKAGE = DBD::mysql::st

int
dataseek(sth, pos)
    SV* sth
    int pos
  PROTOTYPE: $$
  CODE:
{
  D_imp_sth(sth);
  if (imp_sth->cda) {
    mysql_data_seek(imp_sth->cda, pos);
    RETVAL = 1;
  } else {
    RETVAL = 0;
    do_error(sth, JW_ERR_NOT_ACTIVE, "Statement not active");
  }
}
  OUTPUT:
    RETVAL


void
rows(sth)
    SV* sth
  CODE:
    D_imp_sth(sth);
    char buf[64];
    sprintf(buf, "%lu", imp_sth->row_num);
    ST(0) = sv_2mortal(newSVpvn(buf, strlen(buf)));
