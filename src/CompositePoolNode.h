
/*
 * $Id: CompositePoolNode.h,v 1.8 2007/05/29 13:31:36 amosjeffries Exp $
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

#ifndef COMPOSITEPOOLNODE_H
#define COMPOSITEPOOLNODE_H

#if DELAY_POOLS
#include "squid.h"
#include "DelayPools.h"
#include "DelayIdComposite.h"
#include "CommRead.h"

class StoreEntry;

class AuthUserRequest;

class CompositePoolNode : public RefCountable, public Updateable
{

public:
    typedef RefCount<CompositePoolNode> Pointer;
    void *operator new(size_t);
    void operator delete (void *);
    virtual ~CompositePoolNode(){}

    virtual void stats(StoreEntry * sentry) =0;
    virtual void dump(StoreEntry *entry) const =0;
    virtual void update(int incr) =0;
    virtual void parse() = 0;

    class CompositeSelectionDetails;
    virtual DelayIdComposite::Pointer id(CompositeSelectionDetails &) = 0;
    void delayRead(DeferredRead const &);

    class CompositeSelectionDetails
    {

    public:
        CompositeSelectionDetails() {}

        struct IN_ADDR src_addr;
        AuthUserRequest *user;
        String tag;
    };

protected:
    void kickReads();
    DeferredReadManager deferredReads;
};

#endif
#endif /* COMPOSITEPOOLNODE_H */
