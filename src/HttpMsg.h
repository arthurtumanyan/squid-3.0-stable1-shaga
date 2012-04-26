
/*
 * $Id: HttpMsg.h,v 1.16 2007/08/13 17:20:51 hno Exp $
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

#ifndef SQUID_HTTPMSG_H
#define SQUID_HTTPMSG_H

#include "typedefs.h"
#include "HttpHeader.h"
#include "HttpVersion.h"
#include "BodyPipe.h"

// common parts of HttpRequest and HttpReply

class HttpMsg
{

public:
    HttpMsg(http_hdr_owner_type owner);
    virtual ~HttpMsg();

    virtual void reset() = 0; // will have body when http*Clean()s are gone

    void packInto(Packer * p, bool full_uri) const;

    virtual HttpMsg *_lock();	// please use HTTPMSGLOCK()
    virtual void _unlock();	// please use HTTPMSGUNLOCK()

public:
    HttpVersion http_ver;

    HttpHeader header;

    HttpHdrCc *cache_control;

    /* Unsupported, writable, may disappear/change in the future
     * For replies, sums _stored_ status-line, headers, and <CRLF>.
     * Also used to report parsed header size if parse() is successful */
    int hdr_sz;

    int64_t content_length;

    protocol_t protocol;

    HttpMsgParseState pstate;   /* the current parsing state */

    BodyPipe::Pointer body_pipe; // optional pipeline to receive message body

    // returns true and sets hdr_sz on success
    // returns false and sets *error to zero when needs more data
    // returns false and sets *error to a positive http_status code on error
    bool parse(MemBuf *buf, bool eol, http_status *error);

    bool parseCharBuf(const char *buf, ssize_t end);

    int httpMsgParseStep(const char *buf, int len, int atEnd);

    virtual int httpMsgParseError();

    virtual bool expectingBody(method_t, int64_t&) const = 0;

    void firstLineBuf(MemBuf&);

protected:
    virtual bool sanityCheckStartLine(MemBuf *buf, http_status *error) = 0;

    virtual void packFirstLineInto(Packer * p, bool full_uri) const = 0;

    virtual bool parseFirstLine(const char *blk_start, const char *blk_end) = 0;

    virtual void hdrCacheInit();

    int lock_count;

};

/* Temporary parsing state; might turn into the replacement parser later on */
struct _HttpParser {
	char state;
	const char *buf;
	int bufsiz;
	int req_start, req_end;
	int hdr_start, hdr_end;
	int m_start, m_end;
	int u_start, u_end;
	int v_start, v_end;
	int v_maj, v_min;
};
typedef struct _HttpParser HttpParser;

extern void HttpParserInit(HttpParser *, const char *buf, int len);
extern int HttpParserParseReqLine(HttpParser *hp);

#define MSGDODEBUG 0
#if MSGDODEBUG
extern int HttpParserReqSz(HttpParser *);
extern int HttpParserHdrSz(HttpParser *);
extern const char * HttpParserHdrBuf(HttpParser *);
extern int HttpParserRequestLen(HttpParser *hp);
#else
#define	HttpParserReqSz(hp)	( (hp)->req_end - (hp)->req_start + 1 )
#define	HttpParserHdrSz(hp)	( (hp)->hdr_end - (hp)->hdr_start + 1 )
#define	HttpParserHdrBuf(hp)	( (hp)->buf + (hp)->hdr_start )
#define	HttpParserRequestLen(hp)	( (hp)->hdr_end - (hp)->req_start + 1 )
#endif

SQUIDCEXTERN int httpMsgIsolateHeaders(const char **parse_start, int len, const char **blk_start, const char **blk_end);

#define HTTPMSGUNLOCK(a) if(a){(a)->_unlock();(a)=NULL;}
#define HTTPMSGLOCK(a) (a)->_lock()

#endif /* SQUID_HTTPMSG_H */
