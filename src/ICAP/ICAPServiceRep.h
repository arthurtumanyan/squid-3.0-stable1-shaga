
/*
 * $Id: ICAPServiceRep.h,v 1.11 2007/07/24 16:43:33 rousskov Exp $
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
 */

#ifndef SQUID_ICAPSERVICEREP_H
#define SQUID_ICAPSERVICEREP_H

#include "cbdata.h"
#include "ICAPInitiator.h"
#include "ICAPElements.h"

class ICAPOptions;
class ICAPOptXact;

/* The ICAP service representative maintains information about a single ICAP
   service that Squid communicates with. The representative initiates OPTIONS
   requests to the service to keep cached options fresh. One ICAP server may
   host many ICAP services. */

/*
 * A service with a fresh cached OPTIONS response and without many failures
 * is an "up" service. All other services are "down". A service is "probed"
 * if we tried to get an OPTIONS response from it and succeeded or failed.
 * A probed down service is called "broken".
 *
 * The number of failures required to bring an up service down is determined
 * by icap_service_failure_limit in squid.conf.
 *
 * As a bootstrapping mechanism, ICAP transactions wait for an unprobed
 * service to get a fresh OPTIONS response (see the callWhenReady method).
 * The waiting callback is called when the OPTIONS transaction completes,
 * even if the service is now broken.
 *
 * We do not initiate ICAP transactions with a broken service, but will
 * eventually retry to fetch its options in hope to bring the service up.
 *
 * A service that should no longer be used after Squid reconfiguration is
 * treated as if it does not have a fresh cached OPTIONS response. We do 
 * not try to fetch fresh options for such a service. It should be 
 * auto-destroyed by refcounting when no longer used.
 */


class ICAPServiceRep : public RefCountable, public ICAPInitiator
{

public:
    typedef RefCount<ICAPServiceRep> Pointer;

public:
    ICAPServiceRep();
    virtual ~ICAPServiceRep();

    bool configure(Pointer &aSelf); // needs self pointer for ICAPOptXact
    void invalidate(); // call when the service is no longer needed or valid

    const char *methodStr() const;
    const char *vectPointStr() const;

    bool probed() const; // see comments above
    bool broken() const; // see comments above
    bool up() const; // see comments above

    typedef void Callback(void *data, Pointer &service);
    void callWhenReady(Callback *cb, void *data);

    // the methods below can only be called on an up() service
    bool wantsUrl(const String &urlPath) const;
    bool wantsPreview(const String &urlPath, size_t &wantedSize) const;
    bool allows204() const;

    void noteFailure(); // called by transactions to report service failure

public:
    String key;
    ICAP::Method method;
    ICAP::VectPoint point;
    String uri;    // service URI

    // URI components
    String host;
    int port;
    String resource;

    // XXX: use it when selecting a service and handling ICAP errors!
    bool bypass;

public: // treat these as private, they are for callbacks only
    void noteTimeToUpdate();
    void noteTimeToNotify();

    // receive either an ICAP OPTIONS response header or an abort message
    virtual void noteIcapAnswer(HttpMsg *msg);
    virtual void noteIcapQueryAbort(bool);

private:
    // stores Prepare() callback info

    struct Client
    {
        Pointer service; // one for each client to preserve service
        Callback *callback;
        void *data;
    };

    typedef Vector<Client> Clients;
    Clients theClients; // all clients waiting for a call back

    ICAPOptions *theOptions;
    ICAPInitiate *theOptionsFetcher; // pending ICAP OPTIONS transaction
    time_t theLastUpdate; // time the options were last updated

    static const int TheSessionFailureLimit;
    int theSessionFailures;
    const char *isSuspended; // also stores suspension reason for debugging

    bool notifying; // may be true in any state except for the initial
    bool updateScheduled; // time-based options update has been scheduled

private:
    ICAP::Method parseMethod(const char *) const;
    ICAP::VectPoint parseVectPoint(const char *) const;

    void suspend(const char *reason);

    bool hasOptions() const;
    bool needNewOptions() const;
    time_t optionsFetchTime() const;

    void scheduleUpdate(time_t when);
    void scheduleNotification();

    void startGettingOptions();
    void handleNewOptions(ICAPOptions *newOptions);
    void changeOptions(ICAPOptions *newOptions);
    void checkOptions();

    void announceStatusChange(const char *downPhrase, bool important) const;

    const char *status() const;

    Pointer self;
    mutable bool wasAnnouncedUp; // prevent sequential same-state announcements
    CBDATA_CLASS2(ICAPServiceRep);
};


#endif /* SQUID_ICAPSERVICEREP_H */
