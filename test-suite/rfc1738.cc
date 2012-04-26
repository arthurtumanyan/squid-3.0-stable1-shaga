
/*
 * $Id: rfc1738.cc,v 1.1 2003/04/22 01:37:44 robertc Exp $
 *
 * DEBUG: section xx    RFC 1738 string [un]escaping
 * AUTHOR:  Robert Collins
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
 * Copyright (c) 2003 Robert Collins <robertc@squid-cache.org>
 */

#include "squid.h"
#include "util.h"
/* testing: 
SQUIDCEXTERN char *rfc1738_escape(const char *);
SQUIDCEXTERN char *rfc1738_escape_unescaped(const char *);
SQUIDCEXTERN char *rfc1738_escape_part(const char *);
SQUIDCEXTERN void rfc1738_unescape(char *);
*/

/* A corrupt string */
#define BADSTRING "An Escaped %1"
/* A partly corrupt string */
#define CASE1 "An escaped %1A%3"
#define RESULT1 "An escaped \032%3"
/* A non corrupt string */
#define GOODSTRING "An Escaped %1A"
#define GOODUSTRING "An Escaped \032"

void
testAString(char const *aString, char const *aResult)
{
    char *escapedString;
    escapedString = xstrdup (aString);
    rfc1738_unescape(escapedString);
    if (strcmp(escapedString, aResult))
	exit (1);
    safe_free (escapedString);
}

void
testUnescaping()
{
    testAString(BADSTRING,BADSTRING);
    testAString(GOODSTRING, GOODUSTRING);
    testAString(CASE1, RESULT1);
}

int
main (int argc, char **argv)
{
    testUnescaping();
    return 0;
}
