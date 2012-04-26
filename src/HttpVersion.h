
/*
 * $Id: HttpVersion.h,v 1.3 2004/12/11 22:07:31 hno Exp $
 *
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
 * Copyright (c) 2003, Robert Collins <robertc@squid-cache.org>
 */

#ifndef SQUID_HTTPVERSION_H
#define SQUID_HTTPVERSION_H

class HttpVersion
{

public:
    HttpVersion()
    {
        major = 0;
        minor = 0;
    }

    HttpVersion(unsigned int aMajor, unsigned int aMinor)
    {
        major = aMajor;
        minor = aMinor;
    }

    unsigned int major;
    unsigned int minor;

    bool operator==(const HttpVersion& that) const
    {
        if (this->major != that.major)
            return false;

        if (this->minor != that.minor)
            return false;

        return true;
    }

    bool operator!=(const HttpVersion& that) const
    {
        return ((this->major != that.major) || (this->minor != that.minor));
    }

};

#endif /* SQUID_HTTPVERSION_H */
