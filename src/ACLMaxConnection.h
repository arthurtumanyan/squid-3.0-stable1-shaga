
/*
 * $Id: ACLMaxConnection.h,v 1.4 2005/05/06 01:57:55 hno Exp $
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
 *
 * Copyright (c) 2003, Robert Collins <robertc@squid-cache.org>
 */

#ifndef SQUID_ACLMAXCONNECTION_H
#define SQUID_ACLMAXCONNECTION_H
#include "ACL.h"
#include "ACLChecklist.h"

class ACLMaxConnection : public ACL
{

public:
    MEMPROXY_CLASS(ACLMaxConnection);

    ACLMaxConnection(char const *);
    ACLMaxConnection(ACLMaxConnection const &);
    ~ACLMaxConnection();
    ACLMaxConnection&operator=(ACLMaxConnection const &);

    virtual ACL *clone()const;
    virtual char const *typeString() const;
    virtual void parse();
    virtual int match(ACLChecklist *checklist);
    virtual wordlist *dump() const;
    virtual bool empty () const;
    virtual bool valid () const;
    virtual void prepareForUse();

protected:
    static Prototype RegistryProtoype;
    static ACLMaxConnection RegistryEntry_;
    char const *class_;
    int limit;
};

MEMPROXY_CLASS_INLINE(ACLMaxConnection)

#endif /* SQUID_ACLMAXCONNECTION_H */
