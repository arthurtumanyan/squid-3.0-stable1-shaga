
/*
 * $Id: unlinkd.cc,v 1.64 2007/04/30 16:56:09 wessels Exp $
 *
 * DEBUG: section 2     Unlink Daemon
 * AUTHOR: Duane Wessels
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
#include "SquidTime.h"
#include "fde.h"
#include "xusleep.h"

/* This code gets linked to Squid */

static int unlinkd_wfd = -1;
static int unlinkd_rfd = -1;

static void * hIpc;
static pid_t pid;

#define UNLINKD_QUEUE_LIMIT 20

void
unlinkdUnlink(const char *path)
{
    char buf[MAXPATHLEN];
    int l;
    int x;
    static int queuelen = 0;

    if (unlinkd_wfd < 0) {
        debug_trap("unlinkdUnlink: unlinkd_wfd < 0");
        safeunlink(path, 0);
        return;
    }

    /*
     * If the queue length is greater than our limit, then we pause
     * for a small amount of time, hoping that unlinkd has some
     * feedback for us.  Maybe it just needs a slice of the CPU's
     * time.
     */
    if (queuelen >= UNLINKD_QUEUE_LIMIT) {
#if defined(USE_EPOLL) || defined(USE_KQUEUE)
	/*
	 * DPW 2007-04-23
	 * We can't use fd_set when using epoll() or kqueue().  In
	 * these cases we block for 10 ms.
	 */
	xusleep(10000);
#else
	/*
	 * DPW 2007-04-23
	 * When we can use select, block for up to 100 ms.
	 */
        struct timeval to;
        fd_set R;
        FD_ZERO(&R);
        FD_SET(unlinkd_rfd, &R);
        to.tv_sec = 0;
        to.tv_usec = 100000;
        select(unlinkd_rfd + 1, &R, NULL, NULL, &to);
#endif
    }

    /*
    * If there is at least one outstanding unlink request, then
    * try to read a response.  If there's nothing to read we'll
    * get an EWOULDBLOCK or whatever.  If we get a response, then
    * decrement the queue size by the number of newlines read.
    */
    if (queuelen > 0) {
        int x;
        int i;
        char rbuf[512];
        x = read(unlinkd_rfd, rbuf, 511);

        if (x > 0) {
            rbuf[x] = '\0';

            for (i = 0; i < x; i++)
                if ('\n' == rbuf[i])
                    queuelen--;

            assert(queuelen >= 0);
        }
    }

    l = strlen(path);
    assert(l < MAXPATHLEN);
    xstrncpy(buf, path, MAXPATHLEN);
    buf[l++] = '\n';
    x = write(unlinkd_wfd, buf, l);

    if (x < 0) {
        debugs(2, 1, "unlinkdUnlink: write FD " << unlinkd_wfd << " failed: " << xstrerror());
        safeunlink(path, 0);
        return;
    } else if (x != l) {
        debugs(2, 1, "unlinkdUnlink: FD " << unlinkd_wfd << " only wrote " << x << " of " << l << " bytes");
        safeunlink(path, 0);
        return;
    }

    statCounter.unlink.requests++;
    /*
    * Increment this syscalls counter here, even though the syscall
    * is executed by the helper process.  We try to be consistent
    * in counting unlink operations.
    */
    statCounter.syscalls.disk.unlinks++;
    queuelen++;
}

void
unlinkdClose(void)
#ifdef _SQUID_MSWIN_
{

    if (unlinkd_wfd > -1)
    {
        debugs(2, 1, "Closing unlinkd pipe on FD " << unlinkd_wfd);
        shutdown(unlinkd_wfd, SD_BOTH);
        comm_close(unlinkd_wfd);

        if (unlinkd_wfd != unlinkd_rfd)
            comm_close(unlinkd_rfd);

        unlinkd_wfd = -1;

        unlinkd_rfd = -1;
    } else
        debugs(2, 0, "unlinkdClose: WARNING: unlinkd_wfd is " << unlinkd_wfd);

    if (hIpc)
    {
        if (WaitForSingleObject(hIpc, 5000) != WAIT_OBJECT_0) {
            getCurrentTime();
            debugs(2, 1, "unlinkdClose: WARNING: (unlinkd," << pid << "d) didn't exit in 5 seconds");
        }

        CloseHandle(hIpc);
    }
}
#else
{

    if (unlinkd_wfd < 0)
        return;

    debugs(2, 1, "Closing unlinkd pipe on FD " << unlinkd_wfd);

    file_close(unlinkd_wfd);

    if (unlinkd_wfd != unlinkd_rfd)
        file_close(unlinkd_rfd);

    unlinkd_wfd = -1;

    unlinkd_rfd = -1;
}

#endif

void
unlinkdInit(void)
{
    const char *args[2];

    args[0] = "(unlinkd)";
    args[1] = NULL;
    pid = ipcCreate(
#if USE_POLL && defined(_SQUID_OSF_)
              /* pipes and poll() don't get along on DUNIX -DW */
              IPC_STREAM,
#elif defined(_SQUID_MSWIN_)
/* select() will fail on a pipe */
IPC_TCP_SOCKET,
#else
/* We currently need to use FIFO.. see below */
IPC_FIFO,
#endif
              Config.Program.unlinkd,
              args,
              "unlinkd",
              &unlinkd_rfd,
              &unlinkd_wfd,
              &hIpc);

    if (pid < 0)
        fatal("Failed to create unlinkd subprocess");

    xusleep(250000);

    fd_note(unlinkd_wfd, "squid -> unlinkd");

    fd_note(unlinkd_rfd, "unlinkd -> squid");

    commSetTimeout(unlinkd_rfd, -1, NULL, NULL);

    commSetTimeout(unlinkd_wfd, -1, NULL, NULL);

    /*
    * unlinkd_rfd should already be non-blocking because of
    * ipcCreate.  We change unlinkd_wfd to blocking mode because
    * we never want to lose an unlink request, and we don't have
    * code to retry if we get EWOULDBLOCK.  Unfortunately, we can
    * do this only for the IPC_FIFO case.
    */
    assert(fd_table[unlinkd_rfd].flags.nonblocking);

    if (FD_PIPE == fd_table[unlinkd_wfd].type)
        commUnsetNonBlocking(unlinkd_wfd);

    debugs(2, 1, "Unlinkd pipe opened on FD " << unlinkd_wfd);

#ifdef _SQUID_MSWIN_

    debugs(2, 4, "Unlinkd handle: 0x" << std::hex << hIpc << std::dec << ", PID: " << pid);

#endif

}
