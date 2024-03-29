
/*
 * $Id: client_side_reply.h,v 1.17 2007/05/09 07:36:24 wessels Exp $
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

#ifndef SQUID_CLIENTSIDEREPLY_H
#define SQUID_CLIENTSIDEREPLY_H

#include "RefCount.h"
#include "HttpHeader.h"
#include "clientStream.h"
#include "StoreClient.h"
#include "client_side_request.h"


class ErrorState;

/* XXX make static method */

class clientReplyContext : public RefCountable, public StoreClient
{

public:
    void *operator new (size_t byteCount);
    void operator delete (void *address);
    static STCB CacheHit;
    static STCB HandleIMSReply;
    static STCB SendMoreData;

    clientReplyContext(ClientHttpRequest *);
    ~clientReplyContext();

    void saveState();
    void restoreState();
    void purgeRequest ();
    void purgeRequestFindObjectToPurge();
    void purgeDoMissPurge();
    void purgeFoundGet(StoreEntry *newEntry);
    void purgeFoundHead(StoreEntry *newEntry);
    void purgeFoundObject(StoreEntry *entry);
    void sendClientUpstreamResponse();
    void purgeDoPurgeGet(StoreEntry *entry);
    void purgeDoPurgeHead(StoreEntry *entry);
    void doGetMoreData();
    void identifyStoreObject();
    void identifyFoundObject(StoreEntry *entry);
    int storeOKTransferDone() const;
    int storeNotOKTransferDone() const;

    void setReplyToError(err_type, http_status, method_t, char const *, struct IN_ADDR *, HttpRequest *, char *, AuthUserRequest *);
    void createStoreEntry(method_t m, request_flags flags);
    void removeStoreReference(store_client ** scp, StoreEntry ** ep);
    void removeClientStoreReference(store_client **scp, ClientHttpRequest *http);
    void startError(ErrorState * err);
    void processExpired();
    clientStream_status_t replyStatus();
    void processMiss();
    void traceReply(clientStreamNode * node);

    http_status purgeStatus;

    /* state variable - replace with class to handle storeentries at some point */
    int lookingforstore;
    virtual void created (StoreEntry *newEntry);

    ClientHttpRequest *http;
    int headers_sz;
    store_client *sc;		/* The store_client we're using */
    StoreIOBuffer tempBuffer;	/* For use in validating requests via IMS */
    int old_reqsize;		/* ... again, for the buffer */
    size_t reqsize;
    off_t reqofs;
    char tempbuf[HTTP_REQBUF_SZ];	/* a temporary buffer if we need working storage */
#if USE_CACHE_DIGESTS

    const char *lookup_type;	/* temporary hack: storeGet() result: HIT/MISS/NONE */
#endif

    struct
    {

unsigned storelogiccomplete: 1;

unsigned complete: 1;		/* we have read all we can from upstream */
        bool headersSent;
    }

    flags;
    clientStreamNode *ourNode;	/* This will go away if/when this file gets refactored some more */

private:
    CBDATA_CLASS(clientReplyContext);
    clientStreamNode *getNextNode() const;
    void makeThisHead();
    bool errorInStream(StoreIOBuffer const &result, size_t const &sizeToProcess)const ;
    void sendStreamError(StoreIOBuffer const &result);
    void pushStreamData(StoreIOBuffer const &result, char *source);
    void waitForMoreData ();
    clientStreamNode * next() const;
    void startSendProcess();
    StoreIOBuffer holdingBuffer;
    HttpReply *reply;
    void processReplyAccess();
    static PF ProcessReplyAccessResult;
    void processReplyAccessResult(bool accessAllowed);
    void buildReply(const char *buf, size_t size);
    void buildReplyHeader ();
    bool alwaysAllowResponse(http_status sline) const;
    int checkTransferDone();
    void processOnlyIfCachedMiss();
    void cacheHit(StoreIOBuffer result);
    void handleIMSReply(StoreIOBuffer result);
    void sendMoreData(StoreIOBuffer result);
    void triggerInitialStoreRead();
    void sendClientOldEntry();
    void buildMaxBodySize(HttpReply * reply);


    StoreEntry *old_entry;
    store_client *old_sc;	/* ... for entry to be validated */
    bool deleting;
};

#endif /* SQUID_CLIENTSIDEREPLY_H */
