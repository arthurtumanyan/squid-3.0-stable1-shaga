
/*
 * $Id: DelayPool.h,v 1.2 2003/02/21 22:50:05 robertc Exp $
 *
 * DEBUG: section 77    Delay Pools
 * AUTHOR: Robert Collins <robertc@squid-cache.org>
 * Based upon original delay pools code by
 *   David Luyer <david@luyer.net>
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

#include "config.h"

#ifndef DELAYPOOL_H
#define DELAYPOOL_H

#if DELAY_POOLS
#include "squid.h"
#include "CompositePoolNode.h"

class StoreEntry;

class CommonPool;

class acl_access;

class DelayPool
{

public:
    DelayPool();
    ~DelayPool();
    void freeData();
    void createPool(u_char delay_class);
    void parse();
    void dump (StoreEntry *, unsigned int poolNumberMinusOne) const;
    CommonPool *pool;
    CompositePoolNode::Pointer theComposite() {return theComposite_;}

    CompositePoolNode::Pointer const theComposite() const{return theComposite_;}

    acl_access *access;

private:
    CompositePoolNode::Pointer theComposite_;
};

#endif
#endif /* DELAYPOOL_H */
