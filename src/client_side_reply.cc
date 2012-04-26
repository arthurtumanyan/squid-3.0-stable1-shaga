
/*
 * $Id: client_side_reply.cc,v 1.144 2007/11/27 09:36:07 amosjeffries Exp $
 *
 * DEBUG: section 88    Client-side Reply Routines
 * AUTHOR: Robert Collins (Originally Duane Wessels in client_side.c)
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

#include "squid.h"
#include "client_side_reply.h"
#include "errorpage.h"
#include "StoreClient.h"
#include "Store.h"
#include "HttpReply.h"
#include "HttpRequest.h"
#include "forward.h"

#include "clientStream.h"
#include "AuthUserRequest.h"
#if USE_SQUID_ESI
#include "ESI.h"
#endif
#include "MemObject.h"
#include "ACLChecklist.h"
#include "ACL.h"
#if DELAY_POOLS
#include "DelayPools.h"
#endif
#include "client_side.h"
#include "SquidTime.h"

CBDATA_CLASS_INIT(clientReplyContext);

/* Local functions */
extern "C" CSS clientReplyStatus;
extern ErrorState *clientBuildError(err_type, http_status, char const *,

                                        struct IN_ADDR *, HttpRequest *);

/* privates */

clientReplyContext::~clientReplyContext()
{
    deleting = true;
    /* This may trigger a callback back into SendMoreData as the cbdata
     * is still valid
     */
    removeClientStoreReference(&sc, http);
    /* old_entry might still be set if we didn't yet get the reply
     * code in HandleIMSReply() */
    removeStoreReference(&old_sc, &old_entry);
    safe_free(tempBuffer.data);
    cbdataReferenceDone(http);
    HTTPMSGUNLOCK(reply);
}

clientReplyContext::clientReplyContext(ClientHttpRequest *clientContext) : http (cbdataReference(clientContext)), old_entry (NULL), old_sc(NULL), deleting(false)
{}

/* create an error in the store awaiting the client side to read it. */
/* This may be better placed in the clientStream logic, but it has not been
 * relocated there yet
 */
void
clientReplyContext::setReplyToError(
    err_type err, http_status status, method_t method, char const *uri,
    struct IN_ADDR *addr, HttpRequest * failedrequest, char *unparsedrequest,
    AuthUserRequest * auth_user_request)
{
    ErrorState *errstate =
        clientBuildError(err, status, uri, addr, failedrequest);

    if (unparsedrequest)
        errstate->request_hdrs = xstrdup(unparsedrequest);

    if (status == HTTP_NOT_IMPLEMENTED && http->request)
        /* prevent confusion over whether we default to persistent or not */
        http->request->flags.proxy_keepalive = 0;

    http->al.http.code = errstate->httpStatus;

    createStoreEntry(method, request_flags());

    if (auth_user_request)
    {
        errstate->auth_user_request = auth_user_request;
	AUTHUSERREQUESTLOCK(errstate->auth_user_request, "errstate");
    }

    assert(errstate->callback_data == NULL);
    errorAppendEntry(http->storeEntry(), errstate);
    /* Now the caller reads to get this */
}

void
clientReplyContext::removeStoreReference(store_client ** scp,
        StoreEntry ** ep)
{
    StoreEntry *e;
    store_client *sc = *scp;

    if ((e = *ep) != NULL) {
        *ep = NULL;
        storeUnregister(sc, e, this);
        *scp = NULL;
        e->unlock();
    }
}

void
clientReplyContext::removeClientStoreReference(store_client **scp, ClientHttpRequest *http)
{
    StoreEntry *reference = http->storeEntry();
    removeStoreReference(scp, &reference);
    http->storeEntry(reference);
}

void *
clientReplyContext::operator new (size_t byteCount)
{
    /* derived classes with different sizes must implement their own new */
    assert (byteCount == sizeof (clientReplyContext));
    CBDATA_INIT_TYPE(clientReplyContext);
    return cbdataAlloc(clientReplyContext);
}

void
clientReplyContext::operator delete (void *address)
{
    clientReplyContext * tmp = (clientReplyContext *)address;
    cbdataFree (tmp);
}

void
clientReplyContext::saveState()
{
    assert(old_sc == NULL);
    debugs(88, 3, "clientReplyContext::saveState: saving store context");
    old_entry = http->storeEntry();
    old_sc = sc;
    old_reqsize = reqsize;
    tempBuffer.offset = reqofs;
    /* Prevent accessing the now saved entries */
    http->storeEntry(NULL);
    sc = NULL;
    reqsize = 0;
    reqofs = 0;
}

void
clientReplyContext::restoreState()
{
    assert(old_sc != NULL);
    debugs(88, 3, "clientReplyContext::restoreState: Restoring store context");
    removeClientStoreReference(&sc, http);
    http->storeEntry(old_entry);
    sc = old_sc;
    reqsize = old_reqsize;
    reqofs = tempBuffer.offset;
    /* Prevent accessed the old saved entries */
    old_entry = NULL;
    old_sc = NULL;
    old_reqsize = 0;
    tempBuffer.offset = 0;
}

void
clientReplyContext::startError(ErrorState * err)
{
    createStoreEntry(http->request->method, request_flags());
    triggerInitialStoreRead();
    errorAppendEntry(http->storeEntry(), err);
}

clientStreamNode *
clientReplyContext::getNextNode() const
{
    return (clientStreamNode *)ourNode->node.next->data;
}

/* This function is wrong - the client parameters don't include the
 * header offset
 */
void
clientReplyContext::triggerInitialStoreRead()
{
    /* when confident, 0 becomes reqofs, and then this factors into
     * startSendProcess 
     */
    assert(reqofs == 0);
    StoreIOBuffer tempBuffer (next()->readBuffer.length, 0, next()->readBuffer.data);
    storeClientCopy(sc, http->storeEntry(), tempBuffer, SendMoreData, this);
}

/* there is an expired entry in the store.
 * setup a temporary buffer area and perform an IMS to the origin
 */
void
clientReplyContext::processExpired()
{
    char *url = http->uri;
    StoreEntry *entry = NULL;
    debugs(88, 3, "clientReplyContext::processExpired: '" << http->uri << "'");
    assert(http->storeEntry()->lastmod >= 0);
    /*
     * check if we are allowed to contact other servers
     * @?@: Instead of a 504 (Gateway Timeout) reply, we may want to return 
     *      a stale entry *if* it matches client requirements
     */

    if (http->onlyIfCached()) {
        processOnlyIfCachedMiss();
        return;
    }

    http->request->flags.refresh = 1;
#if STORE_CLIENT_LIST_DEBUG
    /* Prevent a race with the store client memory free routines
     */
    assert(storeClientIsThisAClient(sc, this));
#endif
    /* Prepare to make a new temporary request */
    saveState();
    entry = storeCreateEntry(url,
                             http->log_uri, http->request->flags, http->request->method);
    /* NOTE, don't call StoreEntry->lock(), storeCreateEntry() does it */
    sc = storeClientListAdd(entry, this);
#if DELAY_POOLS
    /* delay_id is already set on original store client */
    sc->setDelayId(DelayId::DelayClient(http));
#endif

    http->request->lastmod = old_entry->lastmod;
    debugs(88, 5, "clientReplyContext::processExpired : lastmod " << entry->lastmod );
    http->storeEntry(entry);
    assert(http->out.offset == 0);

    /*
     * A refcounted pointer so that FwdState stays around as long as
     * this clientReplyContext does
     */
    FwdState::fwdStart(http->getConn() != NULL ? http->getConn()->fd : -1,
                       http->storeEntry(),
                       http->request);
    /* Register with storage manager to receive updates when data comes in. */

    if (EBIT_TEST(entry->flags, ENTRY_ABORTED))
        debugs(88, 0, "clientReplyContext::processExpired: Found ENTRY_ABORTED object");

    {
        /* start counting the length from 0 */
        StoreIOBuffer tempBuffer(HTTP_REQBUF_SZ, 0, tempbuf);
        storeClientCopy(sc, entry, tempBuffer, HandleIMSReply, this);
    }
}


void
clientReplyContext::sendClientUpstreamResponse()
{
    StoreIOBuffer tempresult;
    removeStoreReference(&old_sc, &old_entry);
    /* here the data to send is the data we just received */
    tempBuffer.offset = 0;
    old_reqsize = 0;
    /* sendMoreData tracks the offset as well.
     * Force it back to zero */
    reqofs = 0;
    assert(!EBIT_TEST(http->storeEntry()->flags, ENTRY_ABORTED));
    /* TODO: provide sendMoreData with the ready parsed reply */
    tempresult.length = reqsize;
    tempresult.data = tempbuf;
    sendMoreData(tempresult);
}

void
clientReplyContext::HandleIMSReply(void *data, StoreIOBuffer result)
{
    clientReplyContext *context = (clientReplyContext *)data;
    context->handleIMSReply(result);
}

void
clientReplyContext::sendClientOldEntry()
{
    /* Get the old request back */
    restoreState();
    /* here the data to send is in the next nodes buffers already */
    assert(!EBIT_TEST(http->storeEntry()->flags, ENTRY_ABORTED));
    /* sendMoreData tracks the offset as well.
     * Force it back to zero */
    reqofs = 0;
    StoreIOBuffer tempresult (reqsize, reqofs, next()->readBuffer.data);
    sendMoreData(tempresult);
}

/* This is the workhorse of the HandleIMSReply callback.
 *
 * It is called when we've got data back from the origin following our
 * IMS request to revalidate a stale entry.
 */
void
clientReplyContext::handleIMSReply(StoreIOBuffer result)
{
    if (deleting)
        return;

    debugs(88, 3, "handleIMSReply: " << http->storeEntry()->url() << ", " << (long unsigned) result.length << " bytes" );

    if (http->storeEntry() == NULL)
        return;

    if (result.flags.error && !EBIT_TEST(http->storeEntry()->flags, ENTRY_ABORTED))
        return;

    /* update size of the request */
    reqsize = result.length + reqofs;

    const http_status status = http->storeEntry()->getReply()->sline.status;

    // request to origin was aborted
    if (EBIT_TEST(http->storeEntry()->flags, ENTRY_ABORTED)) {
        debugs(88, 3, "handleIMSReply: request to origin aborted '" << http->storeEntry()->url() << "', sending old entry to client" );
        http->logType = LOG_TCP_REFRESH_FAIL;
        sendClientOldEntry();
    }

    // we have a partial reply from the origin
    else if (STORE_PENDING == http->storeEntry()->store_status && 0 == status) {
        // header is too large, send old entry

        if (reqsize >= HTTP_REQBUF_SZ) {
            debugs(88, 3, "handleIMSReply: response from origin is too large '" << http->storeEntry()->url() << "', sending old entry to client" );
            http->logType = LOG_TCP_REFRESH_FAIL;
            sendClientOldEntry();
        }

        // everything looks fine, we're just waiting for more data
        else {
            debugs(88, 3, "handleIMSReply: incomplete headers for '" << http->storeEntry()->url() << "', waiting for more data" );
            reqofs = reqsize;
            waitForMoreData();
        }
    }

    // we have a reply from the origin
    else {
        HttpReply *old_rep = (HttpReply *) old_entry->getReply();

        // origin replied 304

        if (status == HTTP_NOT_MODIFIED) {
            http->logType = LOG_TCP_REFRESH_UNMODIFIED;

            // update headers on existing entry
            HttpReply *old_rep = (HttpReply *) old_entry->getReply();
            old_rep->updateOnNotModified(http->storeEntry()->getReply());
            old_entry->timestampsSet();

            // if client sent IMS

            if (http->request->flags.ims) {
                // forward the 304 from origin
                debugs(88, 3, "handleIMSReply: origin replied 304, revalidating existing entry and forwarding 304 to client");
                sendClientUpstreamResponse();
            } else {
                // send existing entry, it's still valid
                debugs(88, 3, "handleIMSReply: origin replied 304, revalidating existing entry and sending " <<
                       old_rep->sline.status << " to client");
                sendClientOldEntry();
            }
        }

        // origin replied with a non-error code
        else if (status > HTTP_STATUS_NONE && status < HTTP_INTERNAL_SERVER_ERROR) {
            // forward response from origin
            http->logType = LOG_TCP_REFRESH_MODIFIED;
            debugs(88, 3, "handleIMSReply: origin replied " << status << ", replacing existing entry and forwarding to client");
            sendClientUpstreamResponse();
        }

        // origin replied with an error
        else {
            // ignore and let client have old entry
            http->logType = LOG_TCP_REFRESH_FAIL;
            debugs(88, 3, "handleIMSReply: origin replied with error " <<
                   status << ", sending old entry (" << old_rep->sline.status << ") to client");
            sendClientOldEntry();
        }
    }
}

extern "C" CSR clientGetMoreData;
extern "C" CSD clientReplyDetach;

/*
 * clientCacheHit should only be called until the HTTP reply headers
 * have been parsed.  Normally this should be a single call, but
 * it might take more than one.  As soon as we have the headers,
 * we hand off to clientSendMoreData, processExpired, or
 * processMiss.
 */
void
clientReplyContext::CacheHit(void *data, StoreIOBuffer result)
{
    clientReplyContext *context = (clientReplyContext *)data;
    context->cacheHit (result);
}

void
clientReplyContext::cacheHit(StoreIOBuffer result)
{
    if (deleting)
        return;

    StoreEntry *e = http->storeEntry();

    HttpRequest *r = http->request;

    debugs(88, 3, "clientCacheHit: " << http->uri << ", " << result.length << " bytes");

    if (http->storeEntry() == NULL) {
        debugs(88, 3, "clientCacheHit: request aborted");
        return;
    } else if (result.flags.error) {
        /* swap in failure */
        debugs(88, 3, "clientCacheHit: swapin failure for " << http->uri);
        http->logType = LOG_TCP_SWAPFAIL_MISS;
        removeClientStoreReference(&sc, http);
        processMiss();
        return;
    }

    if (result.length == 0) {
        /* the store couldn't get enough data from the file for us to id the
         * object
         */
        /* treat as a miss */
        http->logType = LOG_TCP_MISS;
        processMiss();
        return;
    }

    assert(!EBIT_TEST(e->flags, ENTRY_ABORTED));
    /* update size of the request */
    reqsize = result.length + reqofs;

    if (e->getReply()->sline.status == 0) {
        /*
         * we don't have full reply headers yet; either wait for more or
         * punt to clientProcessMiss.
         */

        if (e->mem_status == IN_MEMORY || e->store_status == STORE_OK) {
            processMiss();
        } else if (result.length + reqofs >= HTTP_REQBUF_SZ
                   && http->out.offset == 0) {
            processMiss();
        } else {
            debugs(88, 3, "clientCacheHit: waiting for HTTP reply headers");
            reqofs += result.length;
            assert(reqofs <= HTTP_REQBUF_SZ);
            /* get the next users' buffer */
            StoreIOBuffer tempBuffer;
            tempBuffer.offset = http->out.offset + reqofs;
            tempBuffer.length = next()->readBuffer.length - reqofs;
            tempBuffer.data = next()->readBuffer.data + reqofs;
            storeClientCopy(sc, e,
                            tempBuffer, CacheHit, this);
        }

        return;
    }

    /*
     * Got the headers, now grok them
     */
    assert(http->logType == LOG_TCP_HIT);

    if (strcmp(e->mem_obj->url, urlCanonical(r)) != 0) {
	debugs(33, 1, "clientProcessHit: URL mismatch, '" << e->mem_obj->url << "' != '" << urlCanonical(r) << "'");
        processMiss();
        return;
    }

    switch (varyEvaluateMatch(e, r)) {

    case VARY_NONE:
        /* No variance detected. Continue as normal */
        break;

    case VARY_MATCH:
        /* This is the correct entity for this request. Continue */
        debugs(88, 2, "clientProcessHit: Vary MATCH!");
        break;

    case VARY_OTHER:
        /* This is not the correct entity for this request. We need
         * to requery the cache.
         */
        removeClientStoreReference(&sc, http);
        e = NULL;
        /* Note: varyEvalyateMatch updates the request with vary information
         * so we only get here once. (it also takes care of cancelling loops)
         */
         debugs(88, 2, "clientProcessHit: Vary detected!");
        clientGetMoreData(ourNode, http);
        return;

    case VARY_CANCEL:
        /* varyEvaluateMatch found a object loop. Process as miss */
        debugs(88, 1, "clientProcessHit: Vary object loop!");
        processMiss();
        return;
    }

    if (r->method == METHOD_PURGE) {
        removeClientStoreReference(&sc, http);
        e = NULL;
        purgeRequest();
        return;
    }

    if (e->checkNegativeHit()
#if HTTP_VIOLATIONS
            && !r->flags.nocache_hack
#endif
       ) {
        http->logType = LOG_TCP_NEGATIVE_HIT;
        sendMoreData(result);
    } else if (!Config.onoff.offline && refreshCheckHTTP(e, r) && !http->flags.internal) {
        debugs(88, 5, "clientCacheHit: in refreshCheck() block");
        /*
         * We hold a stale copy; it needs to be validated
         */
        /*
         * The 'need_validation' flag is used to prevent forwarding
         * loops between siblings.  If our copy of the object is stale,
         * then we should probably only use parents for the validation
         * request.  Otherwise two siblings could generate a loop if
         * both have a stale version of the object.
         */
        r->flags.need_validation = 1;

        if (e->lastmod < 0) {
            /*
             * Previous reply didn't have a Last-Modified header,
             * we cannot revalidate it.
             */
            http->logType = LOG_TCP_MISS;
            processMiss();
        } else if (r->flags.nocache) {
            /*
             * This did not match a refresh pattern that overrides no-cache
             * we should honour the client no-cache header.
             */
            http->logType = LOG_TCP_CLIENT_REFRESH_MISS;
            processMiss();
        } else if (r->protocol == PROTO_HTTP) {
            /*
             * Object needs to be revalidated
             * XXX This could apply to FTP as well, if Last-Modified is known.
             */
            processExpired();
        } else {
            /*
             * We don't know how to re-validate other protocols. Handle
             * them as if the object has expired.
             */
            http->logType = LOG_TCP_MISS;
            processMiss();
        }
    } else if (r->flags.ims) {
        /*
         * Handle If-Modified-Since requests from the client
         */

        if (e->getReply()->sline.status != HTTP_OK) {
            debugs(88, 4, "clientCacheHit: Reply code " <<
                   e->getReply()->sline.status << " != 200");
            http->logType = LOG_TCP_MISS;
            processMiss();
        } else if (e->modifiedSince(http->request)) {
            http->logType = LOG_TCP_IMS_HIT;
            sendMoreData(result);
        } else {
            time_t const timestamp = e->timestamp;
            HttpReply *temprep = e->getReply()->make304();
            http->logType = LOG_TCP_IMS_HIT;
            removeClientStoreReference(&sc, http);
            createStoreEntry(http->request->method,
                             request_flags());
            e = http->storeEntry();
            /*
             * Copy timestamp from the original entry so the 304
             * reply has a meaningful Age: header.
             */
            e->timestamp = timestamp;
            e->replaceHttpReply(temprep);
            e->complete();
            /* TODO: why put this in the store and then serialise it and then parse it again.
             * Simply mark the request complete in our context and
             * write the reply struct to the client side
             */
            triggerInitialStoreRead();
        }
    } else {
        /*
         * plain ol' cache hit
         */

#if DELAY_POOLS
        if (e->store_status != STORE_OK)
            http->logType = LOG_TCP_MISS;
        else
#endif
	if (e->mem_status == IN_MEMORY)
            http->logType = LOG_TCP_MEM_HIT;
        else if (Config.onoff.offline)
            http->logType = LOG_TCP_OFFLINE_HIT;

        sendMoreData(result);
    }
}

/*
 * Prepare to fetch the object as it's a cache miss of some kind.
 */
void
clientReplyContext::processMiss()
{
    char *url = http->uri;
    HttpRequest *r = http->request;
    ErrorState *err = NULL;
    debugs(88, 4, "clientProcessMiss: '" << RequestMethodStr[r->method] << " " << url << "'");
    /*
     * We might have a left-over StoreEntry from a failed cache hit
     * or IMS request.
     */

    if (http->storeEntry()) {
        if (EBIT_TEST(http->storeEntry()->flags, ENTRY_SPECIAL)) {
            debugs(88, 0, "clientProcessMiss: miss on a special object (" << url << ").");
            debugs(88, 0, "\tlog_type = " << log_tags[http->logType]);
            http->storeEntry()->dump(1);
        }

        removeClientStoreReference(&sc, http);
    }

    if (r->method == METHOD_PURGE) {
        purgeRequest();
        return;
    }

    if (http->onlyIfCached()) {
        processOnlyIfCachedMiss();
        return;
    }

    /*
     * Deny loops when running in accelerator/transproxy mode.
     */
    if (http->flags.accel && r->flags.loopdetect) {
        http->al.http.code = HTTP_FORBIDDEN;
        err =
            clientBuildError(ERR_ACCESS_DENIED, HTTP_FORBIDDEN, NULL,
                             &http->getConn()->peer.sin_addr, http->request);
        createStoreEntry(r->method, request_flags());
        errorAppendEntry(http->storeEntry(), err);
        triggerInitialStoreRead();
        return;
    } else {
        assert(http->out.offset == 0);
        createStoreEntry(r->method, r->flags);
        triggerInitialStoreRead();

        if (http->redirect.status) {
            HttpReply *rep = new HttpReply;
#if LOG_TCP_REDIRECTS

            http->logType = LOG_TCP_REDIRECT;
#endif

            http->storeEntry()->releaseRequest();
            rep->redirect(http->redirect.status, http->redirect.location);
            http->storeEntry()->replaceHttpReply(rep);
            http->storeEntry()->complete();
            return;
        }

        if (http->flags.internal)
            r->protocol = PROTO_INTERNAL;

        FwdState::fwdStart(http->getConn() != NULL ? http->getConn()->fd : -1,
                           http->storeEntry(),
                           r);
    }
}

/*
 * client issued a request with an only-if-cached cache-control directive;
 * we did not find a cached object that can be returned without
 *     contacting other servers;
 * respond with a 504 (Gateway Timeout) as suggested in [RFC 2068]
 */
void
clientReplyContext::processOnlyIfCachedMiss()
{
    ErrorState *err = NULL;
    debugs(88, 4, "clientProcessOnlyIfCachedMiss: '" <<
           RequestMethodStr[http->request->method] << " " << http->uri << "'");
    http->al.http.code = HTTP_GATEWAY_TIMEOUT;
    err = clientBuildError(ERR_ONLY_IF_CACHED_MISS, HTTP_GATEWAY_TIMEOUT, NULL,
                           &http->getConn()->peer.sin_addr, http->request);
    removeClientStoreReference(&sc, http);
    startError(err);
}

void
clientReplyContext::purgeRequestFindObjectToPurge()
{
    /* Try to find a base entry */
    http->flags.purging = 1;
    lookingforstore = 1;
    StoreEntry::getPublicByRequestMethod(this, http->request, METHOD_GET);
}

void
clientReplyContext::created(StoreEntry *newEntry)
{
    if (lookingforstore == 1)
        purgeFoundGet(newEntry);
    else if (lookingforstore == 2)
        purgeFoundHead(newEntry);
    else if (lookingforstore == 3)
        purgeDoPurgeGet(newEntry);
    else if (lookingforstore == 4)
        purgeDoPurgeHead(newEntry);
    else if (lookingforstore == 5)
        identifyFoundObject(newEntry);
}

void
clientReplyContext::purgeFoundGet(StoreEntry *newEntry)
{
    if (newEntry->isNull()) {
        lookingforstore = 2;
        StoreEntry::getPublicByRequestMethod(this, http->request, METHOD_HEAD);
    } else
        purgeFoundObject (newEntry);
}

void
clientReplyContext::purgeFoundHead(StoreEntry *newEntry)
{
    if (newEntry->isNull())
        purgeDoMissPurge();
    else
        purgeFoundObject (newEntry);
}

void
clientReplyContext::purgeFoundObject(StoreEntry *entry)
{
    assert (entry && !entry->isNull());

    if (EBIT_TEST(entry->flags, ENTRY_SPECIAL)) {
        http->logType = LOG_TCP_DENIED;
        ErrorState *err =
            clientBuildError(ERR_ACCESS_DENIED, HTTP_FORBIDDEN, NULL,
                             &http->getConn()->peer.sin_addr, http->request);
        startError(err);
        return;
    }

    StoreIOBuffer tempBuffer;
    /* Swap in the metadata */
    http->storeEntry(entry);

    http->storeEntry()->lock()

    ;
    http->storeEntry()->createMemObject(http->uri, http->log_uri);

    http->storeEntry()->mem_obj->method = http->request->method;

    sc = storeClientListAdd(http->storeEntry(), this);

    http->logType = LOG_TCP_HIT;

    reqofs = 0;

    tempBuffer.offset = http->out.offset;

    tempBuffer.length = next()->readBuffer.length;

    tempBuffer.data = next()->readBuffer.data;

    storeClientCopy(sc, http->storeEntry(),
                    tempBuffer, CacheHit, this);
}

void
clientReplyContext::purgeRequest()
{
    debugs(88, 3, "Config2.onoff.enable_purge = " <<
           Config2.onoff.enable_purge);

    if (!Config2.onoff.enable_purge) {
        http->logType = LOG_TCP_DENIED;
        ErrorState *err =
            clientBuildError(ERR_ACCESS_DENIED, HTTP_FORBIDDEN, NULL,
                             &http->getConn()->peer.sin_addr, http->request);
        startError(err);
        return;
    }

    /* Release both IP cache */
    ipcacheInvalidate(http->request->host);

    if (!http->flags.purging)
        purgeRequestFindObjectToPurge();
    else
        purgeDoMissPurge();
}

void
clientReplyContext::purgeDoMissPurge()
{
    http->logType = LOG_TCP_MISS;
    lookingforstore = 3;
    StoreEntry::getPublicByRequestMethod(this,http->request, METHOD_GET);
}

void
clientReplyContext::purgeDoPurgeGet(StoreEntry *newEntry)
{
    assert (newEntry);
    /* Move to new() when that is created */
    purgeStatus = HTTP_NOT_FOUND;

    if (!newEntry->isNull()) {
        /* Release the cached URI */
        debugs(88, 4, "clientPurgeRequest: GET '" << newEntry->url() << "'" );
        newEntry->release();
        purgeStatus = HTTP_OK;
    }

    lookingforstore = 4;
    StoreEntry::getPublicByRequestMethod(this, http->request, METHOD_HEAD);
}

void
clientReplyContext::purgeDoPurgeHead(StoreEntry *newEntry)
{
    if (newEntry && !newEntry->isNull()) {
        debugs(88, 4, "clientPurgeRequest: HEAD '" << newEntry->url() << "'" );
        newEntry->release();
        purgeStatus = HTTP_OK;
    }

    /* And for Vary, release the base URI if none of the headers was included in the request */

    if (http->request->vary_headers
            && !strstr(http->request->vary_headers, "=")) {
        StoreEntry *entry = storeGetPublic(urlCanonical(http->request), METHOD_GET);

        if (entry) {
            debugs(88, 4, "clientPurgeRequest: Vary GET '" << entry->url() << "'" );
            entry->release();
            purgeStatus = HTTP_OK;
        }

        entry = storeGetPublic(urlCanonical(http->request), METHOD_HEAD);

        if (entry) {
            debugs(88, 4, "clientPurgeRequest: Vary HEAD '" << entry->url() << "'" );
            entry->release();
            purgeStatus = HTTP_OK;
        }
    }

    /*
     * Make a new entry to hold the reply to be written
     * to the client.
     */
    /* FIXME: This doesn't need to go through the store. Simply
     * push down the client chain
     */
    createStoreEntry(http->request->method, request_flags());

    triggerInitialStoreRead();

    HttpReply *rep = new HttpReply;

    HttpVersion version(1,0);

    rep->setHeaders(version, purgeStatus, NULL, NULL, 0, 0, -1);

    http->storeEntry()->replaceHttpReply(rep);

    http->storeEntry()->complete();
}

void
clientReplyContext::traceReply(clientStreamNode * node)
{
    clientStreamNode *next = (clientStreamNode *)node->node.next->data;
    StoreIOBuffer tempBuffer;
    assert(http->request->max_forwards == 0);
    createStoreEntry(http->request->method, request_flags());
    tempBuffer.offset = next->readBuffer.offset + headers_sz;
    tempBuffer.length = next->readBuffer.length;
    tempBuffer.data = next->readBuffer.data;
    storeClientCopy(sc, http->storeEntry(),
                    tempBuffer, SendMoreData, this);
    http->storeEntry()->releaseRequest();
    http->storeEntry()->buffer();
    HttpReply *rep = new HttpReply;
    HttpVersion version(1,0);
    rep->setHeaders(version, HTTP_OK, NULL, "text/plain",
                    http->request->prefixLen(), 0, squid_curtime);
    http->storeEntry()->replaceHttpReply(rep);
    http->request->swapOut(http->storeEntry());
    http->storeEntry()->complete();
}

#define SENDING_BODY 0
#define SENDING_HDRSONLY 1
int
clientReplyContext::checkTransferDone()
{
    StoreEntry *entry = http->storeEntry();

    if (entry == NULL)
        return 0;

    /*
     * For now, 'done_copying' is used for special cases like
     * Range and HEAD requests.
     */
    if (http->flags.done_copying)
        return 1;

    /*
     * Handle STORE_OK objects.
     * objectLen(entry) will be set proprely.
     * RC: Does objectLen(entry) include the Headers? 
     * RC: Yes.
     */
    if (entry->store_status == STORE_OK) {
        return storeOKTransferDone();
    } else {
        return storeNotOKTransferDone();
    }
}

int
clientReplyContext::storeOKTransferDone() const
{
    if (http->out.offset >= http->storeEntry()->objectLen() - headers_sz) {
	debugs(88,3,HERE << "storeOKTransferDone " <<
	" out.offset=" << http->out.offset <<
	" objectLen()=" << http->storeEntry()->objectLen() <<
	" headers_sz=" << headers_sz);
        return 1;
    }

    return 0;
}

int
clientReplyContext::storeNotOKTransferDone() const
{
    /*
     * Now, handle STORE_PENDING objects
     */
    MemObject *mem = http->storeEntry()->mem_obj;
    assert(mem != NULL);
    assert(http->request != NULL);
    /* mem->reply was wrong because it uses the UPSTREAM header length!!! */
    HttpReply const *reply = mem->getReply();

    if (headers_sz == 0)
        /* haven't found end of headers yet */
        return 0;

    int sending = SENDING_BODY;

    if (reply->sline.status == HTTP_NO_CONTENT ||
            reply->sline.status == HTTP_NOT_MODIFIED ||
            reply->sline.status < HTTP_OK ||
            http->request->method == METHOD_HEAD)
        sending = SENDING_HDRSONLY;

    /*
     * Figure out how much data we are supposed to send.
     * If we are sending a body and we don't have a content-length,
     * then we must wait for the object to become STORE_OK.
     */
    if (reply->content_length < 0)
        return 0;

    int64_t expectedLength = reply->content_length + http->out.headers_sz;

    if (http->out.size < expectedLength)
        return 0;
    else {
	debugs(88,3,HERE << "storeNotOKTransferDone " <<
	" out.size=" << http->out.size <<
	" expectedLength=" << expectedLength);
        return 1;
    }
}


/* A write has completed, what is the next status based on the
 * canonical request data?
 * 1 something is wrong
 * 0 nothing is wrong.
 *
 */
int
clientHttpRequestStatus(int fd, ClientHttpRequest const *http)
{
#if SIZEOF_INT64_T == 4
    if (http->out.size > 0x7FFF0000) {
        debugs(88, 1, "WARNING: closing FD " << fd << " to prevent out.size counter overflow");
        debugs(88, 1, "\tclient " << (inet_ntoa(http->getConn() != NULL ? http->getConn()->peer.sin_addr : no_addr)));
        debugs(88, 1, "\treceived " << http->out.size << " bytes");
        debugs(88, 1, "\tURI " << http->log_uri);
        return 1;
    }

#endif
#if SIZEOF_INT64_T == 4
    if (http->out.offset > 0x7FFF0000) {
        debugs(88, 1, "WARNING: closing FD " << fd < " to prevent out.offset counter overflow");
        debugs(88, 1, "\tclient " << (inet_ntoa(http->getConn() != NULL ? http->getConn()->peer.sin_addr : no_addr)));
        debugs(88, 1, "\treceived " << http->out.size << " bytes, offset " << http->out.offset);
        debugs(88, 1, "\tURI " << http->log_uri);
        return 1;
    }

#endif
    return 0;
}

/* Preconditions:
 * *http is a valid structure.
 * fd is either -1, or an open fd.
 *
 * TODO: enumify this
 *
 * This function is used by any http request sink, to determine the status
 * of the object.
 */
clientStream_status_t
clientReplyStatus(clientStreamNode * aNode, ClientHttpRequest * http)
{
    clientReplyContext *context = dynamic_cast<clientReplyContext *>(aNode->data.getRaw());
    assert (context);
    assert (context->http == http);
    return context->replyStatus();
}

clientStream_status_t
clientReplyContext::replyStatus()
{
    int done;
    /* Here because lower nodes don't need it */

    if (http->storeEntry() == NULL) {
        debugs(88, 5, "clientReplyStatus: no storeEntry");
        return STREAM_FAILED;	/* yuck, but what can we do? */
    }

    if (EBIT_TEST(http->storeEntry()->flags, ENTRY_ABORTED)) {
        /* TODO: Could upstream read errors (result.flags.error) be
         * lost, and result in undersize requests being considered
         * complete. Should we tcp reset such connections ?
         */
        debugs(88, 5, "clientReplyStatus: aborted storeEntry");
        return STREAM_FAILED;
    }

    if ((done = checkTransferDone()) != 0 || flags.complete) {
        debugs(88, 5, "clientReplyStatus: transfer is DONE");
        /* Ok we're finished, but how? */

        if (http->storeEntry()->getReply()->bodySize(http->request->method) < 0) {
            debugs(88, 5, "clientReplyStatus: closing, content_length < 0");
            return STREAM_FAILED;
        }

        if (!done) {
            debugs(88, 5, "clientReplyStatus: closing, !done, but read 0 bytes");
            return STREAM_FAILED;
        }

        if (!http->gotEnough()) {
            debugs(88, 5, "clientReplyStatus: client didn't get all it expected");
            return STREAM_UNPLANNED_COMPLETE;
        }

        if (http->request->flags.proxy_keepalive) {
            debugs(88, 5, "clientReplyStatus: stream complete and can keepalive");
            return STREAM_COMPLETE;
        }

        debugs(88, 5, "clientReplyStatus: stream was not expected to complete!");
        return STREAM_UNPLANNED_COMPLETE;
    }

    if (http->isReplyBodyTooLarge(http->out.offset - 4096)) {
        /* 4096 is a margin for the HTTP headers included in out.offset */
        debugs(88, 5, "clientReplyStatus: client reply body is too large");
        return STREAM_FAILED;
    }

    return STREAM_NONE;
}

/* Responses with no body will not have a content-type header,
 * which breaks the rep_mime_type acl, which
 * coincidentally, is the most common acl for reply access lists.
 * A better long term fix for this is to allow acl matchs on the various
 * status codes, and then supply a default ruleset that puts these 
 * codes before any user defines access entries. That way the user 
 * can choose to block these responses where appropriate, but won't get
 * mysterious breakages.
 */
bool
clientReplyContext::alwaysAllowResponse(http_status sline) const
{
    bool result;

    switch (sline) {

    case HTTP_CONTINUE:

    case HTTP_SWITCHING_PROTOCOLS:

    case HTTP_PROCESSING:

    case HTTP_NO_CONTENT:

    case HTTP_NOT_MODIFIED:
        result = true;
        break;

    default:
        result = false;
    }

    return result;
}

/*
 * filters out unwanted entries from original reply header
 * adds extra entries if we have more info than origin server
 * adds Squid specific entries
 */
void
clientReplyContext::buildReplyHeader()
{
    HttpHeader *hdr = &reply->header;
    int is_hit = logTypeIsATcpHit(http->logType);
    HttpRequest *request = http->request;
#if DONT_FILTER_THESE
    /* but you might want to if you run Squid as an HTTP accelerator */
    /* hdr->delById(HDR_ACCEPT_RANGES); */
    hdr->delById(HDR_ETAG);
#endif

    // TODO: Should ESIInclude.cc that calls removeConnectionHeaderEntries
    // also delete HDR_PROXY_CONNECTION and HDR_KEEP_ALIVE like we do below?

    // XXX: Should HDR_PROXY_CONNECTION by studied instead of HDR_CONNECTION?
    // httpHeaderHasConnDir does that but we do not. Is this is a bug?
    hdr->delById(HDR_PROXY_CONNECTION);
    /* here: Keep-Alive is a field-name, not a connection directive! */
    hdr->delById(HDR_KEEP_ALIVE);
    /* remove Set-Cookie if a hit */

    if (is_hit)
        hdr->delById(HDR_SET_COOKIE);

    /*
     * Be sure to obey the Connection header 
     */
    reply->header.removeConnectionHeaderEntries();

    //    if (request->range)
    //      clientBuildRangeHeader(http, reply);
    /*
     * Add a estimated Age header on cache hits.
     */
    if (is_hit) {
        /*
         * Remove any existing Age header sent by upstream caches
         * (note that the existing header is passed along unmodified
         * on cache misses)
         */
        hdr->delById(HDR_AGE);
        /*
         * This adds the calculated object age. Note that the details of the
         * age calculation is performed by adjusting the timestamp in
         * StoreEntry::timestampsSet(), not here.
         *
         * BROWSER WORKAROUND: IE sometimes hangs when receiving a 0 Age
         * header, so don't use it unless there is a age to report. Please
         * note that Age is only used to make a conservative estimation of
         * the objects age, so a Age: 0 header does not add any useful
         * information to the reply in any case.
         */

        if (NULL == http->storeEntry())
            (void) 0;
        else if (http->storeEntry()->timestamp < 0)
            (void) 0;

        if (EBIT_TEST(http->storeEntry()->flags, ENTRY_SPECIAL)) {
            hdr->delById(HDR_DATE);
            hdr->insertTime(HDR_DATE, squid_curtime);
        } else if (http->storeEntry()->timestamp < squid_curtime) {
            hdr->putInt(HDR_AGE,
                        squid_curtime - http->storeEntry()->timestamp);
            /* Signal old objects.  NB: rfc 2616 is not clear,
             * by implication, on whether we should do this to all
             * responses, or only cache hits.
             * 14.46 states it ONLY applys for heuristically caclulated
             * freshness values, 13.2.4 doesn't specify the same limitation.
             * We interpret RFC 2616 under the combination.
             */
            /* TODO: if maxage or s-maxage is present, don't do this */

            if (squid_curtime - http->storeEntry()->timestamp >= 86400) {
                char tempbuf[512];
                snprintf (tempbuf, sizeof(tempbuf), "%s %s %s",
                          "113", ThisCache,
                          "This cache hit is still fresh and more than 1 day old");
                hdr->putStr(HDR_WARNING, tempbuf);
            }
        }

    }

    /* Filter unproxyable authentication types */
    if (http->logType != LOG_TCP_DENIED &&
            (hdr->has(HDR_WWW_AUTHENTICATE) || hdr->has(HDR_PROXY_AUTHENTICATE))) {
        HttpHeaderPos pos = HttpHeaderInitPos;
        HttpHeaderEntry *e;

        int headers_deleted = 0;
        while ((e = hdr->getEntry(&pos))) {
            if (e->id == HDR_WWW_AUTHENTICATE || e->id == HDR_PROXY_AUTHENTICATE) {
                const char *value = e->value.buf();

                if ((strncasecmp(value, "NTLM", 4) == 0 &&
                        (value[4] == '\0' || value[4] == ' '))
                        ||
                        (strncasecmp(value, "Negotiate", 9) == 0 &&
                         (value[9] == '\0' || value[9] == ' ')))
                            hdr->delAt(pos, headers_deleted);
            }
        }
        if (headers_deleted)
            hdr->refreshMask();
    }

    /* Handle authentication headers */
    if (request->auth_user_request)
        authenticateFixHeader(reply, request->auth_user_request, request,
                              http->flags.accel, 0);

    /* Append X-Cache */
    httpHeaderPutStrf(hdr, HDR_X_CACHE, "%s from %s",
                      is_hit ? "HIT" : "MISS", getMyHostname());

#if USE_CACHE_DIGESTS
    /* Append X-Cache-Lookup: -- temporary hack, to be removed @?@ @?@ */
    httpHeaderPutStrf(hdr, HDR_X_CACHE_LOOKUP, "%s from %s:%d",
                      lookup_type ? lookup_type : "NONE",
                      getMyHostname(), getMyPort());

#endif

    if (reply->bodySize(request->method) < 0) {
        debugs(88, 3, "clientBuildReplyHeader: can't keep-alive, unknown body size" );
        request->flags.proxy_keepalive = 0;
    }

    if (fdUsageHigh()&& !request->flags.must_keepalive) {
        debugs(88, 3, "clientBuildReplyHeader: Not many unused FDs, can't keep-alive");
        request->flags.proxy_keepalive = 0;
    }

    if (!Config.onoff.error_pconns && reply->sline.status >= 400 && !request->flags.must_keepalive) {
        debugs(33, 3, "clientBuildReplyHeader: Error, don't keep-alive");
        request->flags.proxy_keepalive = 0;
    }

    if (!Config.onoff.client_pconns && !request->flags.must_keepalive)
        request->flags.proxy_keepalive = 0;

    /* Append VIA */
    if (Config.onoff.via) {
        LOCAL_ARRAY(char, bbuf, MAX_URL + 32);
        String strVia;
       	hdr->getList(HDR_VIA, &strVia);
        snprintf(bbuf, sizeof(bbuf), "%d.%d %s",
                 reply->sline.version.major,
                 reply->sline.version.minor,
                 ThisCache);
        strListAdd(&strVia, bbuf, ',');
        hdr->delById(HDR_VIA);
        hdr->putStr(HDR_VIA, strVia.buf());
    }
    /* Signal keep-alive if needed */
    hdr->putStr(http->flags.accel ? HDR_CONNECTION : HDR_PROXY_CONNECTION,
                request->flags.proxy_keepalive ? "keep-alive" : "close");

#if ADD_X_REQUEST_URI
    /*
     * Knowing the URI of the request is useful when debugging persistent
     * connections in a client; we cannot guarantee the order of http headers,
     * but X-Request-URI is likely to be the very last header to ease use from a
     * debugger [hdr->entries.count-1].
     */
    hdr->putStr(HDR_X_REQUEST_URI,
                http->memOjbect()->url ? http->memObject()->url : http->uri);

#endif

    httpHdrMangleList(hdr, request, ROR_REPLY);
}


void
clientReplyContext::buildReply(const char *buf, size_t size)
{
    size_t k = headersEnd(buf, size);

    if (!k)
        return;

    assert(reply == NULL);

    HttpReply *rep = new HttpReply;

    reply = HTTPMSGLOCK(rep);

    if (!reply->parseCharBuf(buf, k)) {
        /* parsing failure, get rid of the invalid reply */
        HTTPMSGUNLOCK(reply);

        if (http->request->range) {
            debugs(0,0,HERE << "look for bug here");
            /* this will fail and destroy request->range */
            //          clientBuildRangeHeader(http, reply);
        }

        return;
    }

    /* enforce 1.0 reply version */
    reply->sline.version = HttpVersion(1,0);

    /* do header conversions */
    buildReplyHeader();
}

void
clientReplyContext::identifyStoreObject()
{
    HttpRequest *r = http->request;

    if (r->flags.cachable || r->flags.internal) {
        lookingforstore = 5;
        StoreEntry::getPublicByRequest (this, r);
    } else {
        identifyFoundObject (NullStoreEntry::getInstance());
    }
}

void
clientReplyContext::identifyFoundObject(StoreEntry *newEntry)
{
    StoreEntry *e = newEntry;
    HttpRequest *r = http->request;

    if (e->isNull()) {
        http->storeEntry(NULL);
    } else {
        http->storeEntry(e);
    }

    e = http->storeEntry();
    /* Release IP-cache entries on reload */

    if (r->flags.nocache) {

#if USE_DNSSERVERS

        ipcacheInvalidate(r->host);

#else

        ipcacheInvalidateNegative(r->host);

#endif /* USE_DNSSERVERS */

    }

#if HTTP_VIOLATIONS

    else if (r->flags.nocache_hack) {

#if USE_DNSSERVERS

        ipcacheInvalidate(r->host);

#else

        ipcacheInvalidateNegative(r->host);

#endif /* USE_DNSSERVERS */

    }

#endif /* HTTP_VIOLATIONS */
#if USE_CACHE_DIGESTS

    lookup_type = http->storeEntry() ? "HIT" : "MISS";

#endif

    if (NULL == http->storeEntry()) {
        /* this object isn't in the cache */
        debugs(85, 3, "clientProcessRequest2: storeGet() MISS");
        http->logType = LOG_TCP_MISS;
        doGetMoreData();
        return;
    }

    if (Config.onoff.offline) {
        debugs(85, 3, "clientProcessRequest2: offline HIT");
        http->logType = LOG_TCP_HIT;
        doGetMoreData();
        return;
    }

    if (http->redirect.status) {
        /* force this to be a miss */
        http->storeEntry(NULL);
        http->logType = LOG_TCP_MISS;
        doGetMoreData();
        return;
    }

    if (!e->validToSend()) {
        debugs(85, 3, "clientProcessRequest2: !storeEntryValidToSend MISS" );
        http->storeEntry(NULL);
        http->logType = LOG_TCP_MISS;
        doGetMoreData();
        return;
    }

    if (EBIT_TEST(e->flags, ENTRY_SPECIAL)) {
        /* Special entries are always hits, no matter what the client says */
        debugs(85, 3, "clientProcessRequest2: ENTRY_SPECIAL HIT");
        http->logType = LOG_TCP_HIT;
        doGetMoreData();
        return;
    }

    if (r->flags.nocache) {
        debugs(85, 3, "clientProcessRequest2: no-cache REFRESH MISS");
        http->storeEntry(NULL);
        http->logType = LOG_TCP_CLIENT_REFRESH_MISS;
        doGetMoreData();
        return;
    }

    debugs(85, 3, "clientProcessRequest2: default HIT");
    http->logType = LOG_TCP_HIT;
    doGetMoreData();
}

/* Request more data from the store for the client Stream
 * This is *the* entry point to this module.
 *
 * Preconditions:
 * This is the head of the list.
 * There is at least one more node.
 * data context is not null
 */
void
clientGetMoreData(clientStreamNode * aNode, ClientHttpRequest * http)
{
    /* Test preconditions */
    assert(aNode != NULL);
    assert(cbdataReferenceValid(aNode));
    assert(aNode->node.prev == NULL);
    assert(aNode->node.next != NULL);
    clientReplyContext *context = dynamic_cast<clientReplyContext *>(aNode->data.getRaw());
    assert (context);
    assert(context->http == http);


    clientStreamNode *next = ( clientStreamNode *)aNode->node.next->data;

    if (!context->ourNode)
        context->ourNode = aNode;

    /* no cbdatareference, this is only used once, and safely */
    if (context->flags.storelogiccomplete) {
        StoreIOBuffer tempBuffer;
        tempBuffer.offset = next->readBuffer.offset + context->headers_sz;
        tempBuffer.length = next->readBuffer.length;
        tempBuffer.data = next->readBuffer.data;

        storeClientCopy(context->sc, http->storeEntry(),
                        tempBuffer, clientReplyContext::SendMoreData, context);
        return;
    }

    if (context->http->request->method == METHOD_PURGE) {
        context->purgeRequest();
        return;
    }

    if (context->http->request->method == METHOD_TRACE) {
        if (context->http->request->max_forwards == 0) {
            context->traceReply(aNode);
            return;
        }

        /* continue forwarding, not finished yet. */
        http->logType = LOG_TCP_MISS;

        context->doGetMoreData();
    } else
        context->identifyStoreObject();
}

void
clientReplyContext::doGetMoreData()
{
    /* We still have to do store logic processing - vary, cache hit etc */
    if (http->storeEntry() != NULL) {
        /* someone found the object in the cache for us */
        StoreIOBuffer tempBuffer;

        http->storeEntry()->lock()

        ;

        if (http->storeEntry()->mem_obj == NULL) {
            /*
             * This if-block exists because we don't want to clobber
             * a preexiting mem_obj->method value if the mem_obj
             * already exists.  For example, when a HEAD request
             * is a cache hit for a GET response, we want to keep
             * the method as GET.
             */
            http->storeEntry()->createMemObject(http->uri, http->log_uri);
            http->storeEntry()->mem_obj->method =
                http->request->method;
        }

        sc = storeClientListAdd(http->storeEntry(), this);
#if DELAY_POOLS

        sc->setDelayId(DelayId::DelayClient(http));
#endif

        assert(http->logType == LOG_TCP_HIT);
        reqofs = 0;
        /* guarantee nothing has been sent yet! */
        assert(http->out.size == 0);
        assert(http->out.offset == 0);
        tempBuffer.offset = reqofs;
        tempBuffer.length = getNextNode()->readBuffer.length;
        tempBuffer.data = getNextNode()->readBuffer.data;
        storeClientCopy(sc, http->storeEntry(), tempBuffer, CacheHit, this);
    } else {
        /* MISS CASE, http->logType is already set! */
        processMiss();
    }
}

/* the next node has removed itself from the stream. */
void
clientReplyDetach(clientStreamNode * node, ClientHttpRequest * http)
{
    /* detach from the stream */
    clientStreamDetach(node, http);
}

/*
 * accepts chunk of a http message in buf, parses prefix, filters headers and
 * such, writes processed message to the message recipient
 */
void
clientReplyContext::SendMoreData(void *data, StoreIOBuffer result)
{
    clientReplyContext *context = static_cast<clientReplyContext *>(data);
    context->sendMoreData (result);
}

void
clientReplyContext::makeThisHead()
{
    /* At least, I think thats what this does */
    dlinkDelete(&http->active, &ClientActiveRequests);
    dlinkAdd(http, &http->active, &ClientActiveRequests);
}

bool
clientReplyContext::errorInStream(StoreIOBuffer const &result, size_t const &sizeToProcess)const
{
    return /* aborted request */
        (http->storeEntry() && EBIT_TEST(http->storeEntry()->flags, ENTRY_ABORTED)) ||
        /* Upstream read error */ (result.flags.error) ||
        /* Upstream EOF */ (sizeToProcess == 0);
}

void
clientReplyContext::sendStreamError(StoreIOBuffer const &result)
{
    /* call clientWriteComplete so the client socket gets closed */
    /* We call into the stream, because we don't know that there is a
     * client socket!
     */
     debugs(88, 5, "clientReplyContext::sendStreamError: A stream error has occured, marking as complete and sending no data.");
    StoreIOBuffer tempBuffer;
    flags.complete = 1;
    tempBuffer.flags.error = result.flags.error;
    clientStreamCallback((clientStreamNode*)http->client_stream.head->data, http, NULL,
                         tempBuffer);
}

void
clientReplyContext::pushStreamData(StoreIOBuffer const &result, char *source)
{
    StoreIOBuffer tempBuffer;

    if (result.length == 0) {
        debugs(88, 5, "clientReplyContext::pushStreamData: marking request as complete due to 0 length store result");
        flags.complete = 1;
    }

    assert(result.offset - headers_sz == next()->readBuffer.offset);
    tempBuffer.offset = result.offset - headers_sz;
    tempBuffer.length = result.length;

    if (tempBuffer.length)
        tempBuffer.data = source;

    clientStreamCallback((clientStreamNode*)http->client_stream.head->data, http, NULL,
                         tempBuffer);
}

clientStreamNode *
clientReplyContext::next() const
{
    assert ( (clientStreamNode*)http->client_stream.head->next->data == getNextNode());
    return getNextNode();
}

void
clientReplyContext::waitForMoreData ()
{
    debugs(88, 5, "clientReplyContext::waitForMoreData: Waiting for more data to parse reply headers in client side.");
    /* We don't have enough to parse the metadata yet */
    /* TODO: the store should give us out of band metadata and
     * obsolete this routine 
     */
    /* wait for more to arrive */
    startSendProcess();
}

void
clientReplyContext::startSendProcess()
{
    debugs(88, 5, "clientReplyContext::startSendProcess: triggering store read to SendMoreData");
    assert(reqofs <= HTTP_REQBUF_SZ);
    /* TODO: copy into the supplied buffer */
    StoreIOBuffer tempBuffer;
    tempBuffer.offset = reqofs;
    tempBuffer.length = next()->readBuffer.length - reqofs;
    tempBuffer.data = next()->readBuffer.data + reqofs;
    storeClientCopy(sc, http->storeEntry(),
                    tempBuffer, SendMoreData, this);
}

/*
 * Calculates the maximum size allowed for an HTTP response
 */
void
clientReplyContext::buildMaxBodySize(HttpReply * reply)
{
    acl_size_t *l = Config.ReplyBodySize;
    ACLChecklist *ch;

    if (http->logType == LOG_TCP_DENIED)
        return;

    ch = clientAclChecklistCreate(NULL, http);

    ch->reply = HTTPMSGLOCK(reply);

    for (l = Config.ReplyBodySize; l; l = l -> next) {
        if (ch->matchAclListFast(l->aclList)) {
            if (l->size != -1) {
                debugs(58, 3, "clientReplyContext: Setting maxBodySize to " <<  l->size);
                http->maxReplyBodySize(l->size);
            }

            break;
        }
    }

    delete ch;
}

void
clientReplyContext::processReplyAccess ()
{
    assert(reply);
    buildMaxBodySize(reply);

    /* Dont't block our own responses or HTTP status messages */
    if (http->logType == LOG_TCP_DENIED || http->logType == LOG_TCP_DENIED_REPLY || alwaysAllowResponse(reply->sline.status)) {
        headers_sz = reply->hdr_sz;
	processReplyAccessResult(1);
	return;
    }

    if (http->isReplyBodyTooLarge(reply->content_length)) {
        ErrorState *err =
            clientBuildError(ERR_TOO_BIG, HTTP_FORBIDDEN, NULL,
                             http->getConn() != NULL ? &http->getConn()->peer.sin_addr : &no_addr,
                             http->request);
        removeClientStoreReference(&sc, http);
        HTTPMSGUNLOCK(reply);
        startError(err);
        return;
    }

    headers_sz = reply->hdr_sz;

    if (!Config.accessList.reply) {
	processReplyAccessResult(1);
	return;
    }

    ACLChecklist *replyChecklist;
    replyChecklist = clientAclChecklistCreate(Config.accessList.reply, http);
    replyChecklist->reply = HTTPMSGLOCK(reply);
    replyChecklist->nonBlockingCheck(ProcessReplyAccessResult, this);
}

void
clientReplyContext::ProcessReplyAccessResult (int rv, void *voidMe)
{
    clientReplyContext *me = static_cast<clientReplyContext *>(voidMe);
    me->processReplyAccessResult(rv);
}

void
clientReplyContext::processReplyAccessResult(bool accessAllowed)
{
    debugs(88, 2, "The reply for " << RequestMethodStr[http->request->method] 
           << " " << http->uri << " is " 
           << ( accessAllowed ? "ALLOWED" : "DENIED") 
           << ", because it matched '" 
           << (AclMatchedName ? AclMatchedName : "NO ACL's") << "'" );

    if (!accessAllowed) {
        ErrorState *err;
        err_type page_id;
        page_id = aclGetDenyInfoPage(&Config.denyInfoList, AclMatchedName, 1);

        http->logType = LOG_TCP_DENIED_REPLY;

        if (page_id == ERR_NONE)
            page_id = ERR_ACCESS_DENIED;

        err =
            clientBuildError(page_id, HTTP_FORBIDDEN, NULL,
                             http->getConn() != NULL ? &http->getConn()->peer.sin_addr : &no_addr,
                             http->request);

        removeClientStoreReference(&sc, http);

        HTTPMSGUNLOCK(reply);

        startError(err);


        return;
    }

    /* Ok, the reply is allowed, */
    http->loggingEntry(http->storeEntry());

    ssize_t body_size = reqofs - reply->hdr_sz;

    assert(body_size >= 0);

    debugs(88, 3, "clientReplyContext::sendMoreData: Appending " <<
           (int) body_size << " bytes after " << reply->hdr_sz <<
           " bytes of headers");

#if USE_SQUID_ESI

    if (http->flags.accel && reply->sline.status != HTTP_FORBIDDEN &&
            !alwaysAllowResponse(reply->sline.status) &&
            esiEnableProcessing(reply)) {
        debugs(88, 2, "Enabling ESI processing for " << http->uri);
        clientStreamInsertHead(&http->client_stream, esiStreamRead,
                               esiProcessStream, esiStreamDetach, esiStreamStatus, NULL);
    }

#endif

    if (http->request->method == METHOD_HEAD) {
        /* do not forward body for HEAD replies */
        body_size = 0;
        http->flags.done_copying = 1;
        flags.complete = 1;
    }

    assert (!flags.headersSent);
    flags.headersSent = true;

    StoreIOBuffer tempBuffer;
    char *buf = next()->readBuffer.data;
    char *body_buf = buf + reply->hdr_sz;

    //Server side may disable ranges under some circumstances.

    if ((!http->request->range))
        next()->readBuffer.offset = 0;

    if (next()->readBuffer.offset != 0) {
        if (next()->readBuffer.offset > body_size) {
            /* Can't use any of the body we received. send nothing */
            tempBuffer.length = 0;
            tempBuffer.data = NULL;
        } else {
            tempBuffer.length = body_size - next()->readBuffer.offset;
            tempBuffer.data = body_buf + next()->readBuffer.offset;
        }
    } else {
        tempBuffer.length = body_size;
        tempBuffer.data = body_buf;
    }

    /* TODO??: move the data in the buffer back by the request header size */
    clientStreamCallback((clientStreamNode *)http->client_stream.head->data,
                         http, reply, tempBuffer);

    return;
}

void
clientReplyContext::sendMoreData (StoreIOBuffer result)
{
    if (deleting)
        return;

    StoreEntry *entry = http->storeEntry();

    ConnStateData::Pointer conn = http->getConn();

    int fd = conn != NULL ? conn->fd : -1;

    char *buf = next()->readBuffer.data;

    char *body_buf = buf;

    /* This is always valid until we get the headers as metadata from
     * storeClientCopy. 
     * Then it becomes reqofs == next->readBuffer.offset()
     */
    assert(reqofs == 0 || flags.storelogiccomplete);

    if (flags.headersSent && buf != result.data) {
        /* we've got to copy some data */
        assert(result.length <= next()->readBuffer.length);
        xmemcpy(buf, result.data, result.length);
        body_buf = buf;
    } else if (!flags.headersSent &&
               buf + reqofs !=result.data) {
        /* we've got to copy some data */
        assert(result.length + reqofs <= next()->readBuffer.length);
        xmemcpy(buf + reqofs, result.data, result.length);
        body_buf = buf;
    }

    /* We've got the final data to start pushing... */
    flags.storelogiccomplete = 1;

    reqofs += result.length;

    assert(reqofs <= HTTP_REQBUF_SZ || flags.headersSent);

    assert(http->request != NULL);

    /* ESI TODO: remove this assert once everything is stable */
    assert(http->client_stream.head->data
           && cbdataReferenceValid(http->client_stream.head->data));

    makeThisHead();

    debugs(88, 5, "clientReplyContext::sendMoreData: " << http->uri << ", " <<
           reqofs << " bytes (" << result.length <<
           " new bytes)");
    debugs(88, 5, "clientReplyContext::sendMoreData:"
		" FD " << fd <<
		" '" << entry->url() << "'" <<
		" out.offset=" << http->out.offset);

    /* update size of the request */
    reqsize = reqofs;

    if (errorInStream(result, reqofs)) {
        sendStreamError(result);
        return;
    }

    if (flags.headersSent) {
        pushStreamData (result, buf);
        return;
    }

    buildReply(buf, reqofs);
    ssize_t body_size = reqofs;

    if (reply) {

        /* handle headers */

        if (Config.onoff.log_mime_hdrs) {
            size_t k;

            if ((k = headersEnd(buf, reqofs))) {
                safe_free(http->al.headers.reply);
                http->al.headers.reply = (char *)xcalloc(k + 1, 1);
                xstrncpy(http->al.headers.reply, buf, k);
            }
        }

        holdingBuffer = result;
        processReplyAccess();
        return;

    } else if (reqofs < HTTP_REQBUF_SZ && entry->store_status == STORE_PENDING) {
        waitForMoreData();
        return;
    } else if (http->request->method == METHOD_HEAD) {
        /*
         * If we are here, then store_status == STORE_OK and it
         * seems we have a HEAD repsponse which is missing the
         * empty end-of-headers line (home.mira.net, phttpd/0.99.72
         * does this).  Because buildReply() fails we just
         * call this reply a body, set the done_copying flag and
         * continue...
         */
        /* RBC: Note that this is seriously broken, as we *need* the
         * metadata to allow further client modules to work. As such 
         * webservers are seriously broken, this is probably not 
         * going to get fixed.. perhapos we should remove it?
         */
        debugs(88, 0, "Broken head response - probably phttpd/0.99.72");
        http->flags.done_copying = 1;
        flags.complete = 1;
        /*
         * And as this is a malformed HTTP reply we cannot keep
         * the connection persistent
         */
        http->request->flags.proxy_keepalive = 0;

        assert(body_buf && body_size);
        StoreIOBuffer tempBuffer (body_size, 0 ,body_buf);
        clientStreamCallback((clientStreamNode *)http->client_stream.head->data,
                             http, NULL, tempBuffer);
    } else {
        debugs(88, 0, "clientReplyContext::sendMoreData: Unable to parse reply headers within a single HTTP_REQBUF_SZ length buffer");
        StoreIOBuffer tempBuffer;
        tempBuffer.flags.error = 1;
        /* XXX FIXME: make an html error page here */
        sendStreamError(tempBuffer);
        return;
    }
    fatal ("clientReplyContext::sendMoreData: Unreachable code reached \n");
}



/* Using this breaks the client layering just a little!
 */
void
clientReplyContext::createStoreEntry(method_t m, request_flags flags)
{
    assert(http != NULL);
    /*
     * For erroneous requests, we might not have a h->request,
     * so make a fake one.
     */

    if (http->request == NULL)
        http->request = HTTPMSGLOCK(new HttpRequest(m, PROTO_NONE, null_string));

    StoreEntry *e = storeCreateEntry(http->uri, http->log_uri, flags, m);

    sc = storeClientListAdd(e, this);

#if DELAY_POOLS

    sc->setDelayId(DelayId::DelayClient(http));

#endif

    reqofs = 0;

    reqsize = 0;

    /* I don't think this is actually needed! -- adrian */
    /* http->reqbuf = http->norm_reqbuf; */
    //    assert(http->reqbuf == http->norm_reqbuf);
    /* The next line is illegal because we don't know if the client stream
     * buffers have been set up 
     */
    //    storeClientCopy(http->sc, e, 0, HTTP_REQBUF_SZ, http->reqbuf,
    //        SendMoreData, this);
    /* So, we mark the store logic as complete */
    this->flags.storelogiccomplete = 1;

    /* and get the caller to request a read, from whereever they are */
    /* NOTE: after ANY data flows down the pipe, even one step,
     * this function CAN NOT be used to manage errors 
     */
    http->storeEntry(e);
}

ErrorState *
clientBuildError(err_type page_id, http_status status, char const *url,

                 struct IN_ADDR * src_addr, HttpRequest * request)
{
    ErrorState *err = errorCon(page_id, status, request);
    err->src_addr = *src_addr;

    if (url)
        err->url = xstrdup(url);

    return err;
}
