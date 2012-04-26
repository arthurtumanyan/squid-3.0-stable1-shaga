/*
 * AUTHOR: Arthur Tumanyan 
 *
 * SQUID Web Proxy Cache          http://www.squid-cache.org/
 * ----------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from
 *  the Internet community; see the CONTRIBUTORS file for full
 *  details.   Many organizations have provided support for Squid's
 *  development; see the SPONSORS file for full details.  Squid is
 *  Copyrighted (C) 2001 by the Regents of the University of
 *  California; see the COPYRIGHT file for full details.  Squid
 *  incorporates software developed and/or copyrighted by other
 *  sources; see the CREDITS file for full details.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 *
 */

#include <mysql.h>

#ifndef SHAGA_LIB_H
#define SHAGA_LIB_H

#define LINE_MAXLEN 512
#define TMP_TABLE "tmp_traff"
#define MAX_USER_COUNT 9999
//
#include "ShagaProtos.h"

int state;
int hc = 0;
char TMP_QUERY[LINE_MAXLEN];
char MYSQL_ERR[LINE_MAXLEN];
char TMP_MSG[LINE_MAXLEN];
int exclude;
int u_count; /* users count */
char timestr[25];
struct tm *thistime;

struct {
	char host[16];
}excludeHosts[MAX_USER_COUNT];
//
int shaga_force = 0;
int already_logged = 0;
//
MYSQL_RES *result;
MYSQL_ROW row;
MYSQL *connection, mysql;
//
//
#endif
