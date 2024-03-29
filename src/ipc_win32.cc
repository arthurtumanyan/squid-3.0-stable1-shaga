
/*
 * $Id: ipc_win32.cc,v 1.4 2007/04/30 16:56:09 wessels Exp $
 *
 * DEBUG: section 54    Windows Interprocess Communication
 * AUTHOR: Andrey Shorin <tolsty@tushino.com>
 * AUTHOR: Guido Serassio <serassio@squid-cache.org>
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
#include "comm.h"
#include "fde.h"
#include "SquidTime.h"

#ifndef _MSWSOCK_
#include <mswsock.h>
#endif
#include <process.h>

struct ipc_params
{
    int type;
    int crfd;
    int cwfd;

    struct sockaddr_in PS;
    const char *prog;
    char **args;
};

struct thread_params
{
    int type;
    int rfd;
    int send_fd;
    const char *prog;
    pid_t pid;
};

static unsigned int __stdcall ipc_thread_1(void *params);
static unsigned int __stdcall ipc_thread_2(void *params);

static const char *ok_string = "OK\n";
static const char *err_string = "ERR\n";
static const char *shutdown_string = "$shutdown\n";

static const char *hello_string = "hi there\n";
#define HELLO_BUF_SZ 32
static char hello_buf[HELLO_BUF_SZ];

static int
ipcCloseAllFD(int prfd, int pwfd, int crfd, int cwfd)
{
    if (prfd >= 0)
        comm_close(prfd);

    if (prfd != pwfd)
        if (pwfd >= 0)
            comm_close(pwfd);

    if (crfd >= 0)
        comm_close(crfd);

    if (crfd != cwfd)
        if (cwfd >= 0)
            comm_close(cwfd);

    return -1;
}

static void
PutEnvironment()
{
#if HAVE_PUTENV
    char *env_str;
    int tmp_s;
    env_str = (char *)xcalloc((tmp_s = strlen(Config.debugOptions) + 32), 1);
    snprintf(env_str, tmp_s, "SQUID_DEBUG=%s", Config.debugOptions);
    putenv(env_str);
#endif
}

pid_t
ipcCreate(int type, const char *prog, const char *const args[], const char *name, int *rfd, int *wfd, void **hIpc)
{
    unsigned long thread;

    struct ipc_params params;
    int opt;
    int optlen = sizeof(opt);
    DWORD ecode = 0;
    pid_t pid;

    struct sockaddr_in CS;

    struct sockaddr_in PS;
    int crfd = -1;
    int prfd = -1;
    int cwfd = -1;
    int pwfd = -1;
    socklen_t len;
    int x;

    requirePathnameExists(name, prog);

    if (rfd)
        *rfd = -1;

    if (wfd)
        *wfd = -1;

    if (hIpc)
        *hIpc = NULL;

    if (WIN32_OS_version != _WIN_OS_WINNT) {
        getsockopt(INVALID_SOCKET, SOL_SOCKET, SO_OPENTYPE, (char *) &opt, &optlen);
        opt = opt & ~(SO_SYNCHRONOUS_NONALERT | SO_SYNCHRONOUS_ALERT);
        setsockopt(INVALID_SOCKET, SOL_SOCKET, SO_OPENTYPE, (char *) &opt, sizeof(opt));
    }

    if (type == IPC_TCP_SOCKET) {
        crfd = cwfd = comm_open(SOCK_STREAM,
                                IPPROTO_TCP,
                                local_addr,
                                0,
                                COMM_NOCLOEXEC,
                                name);
        prfd = pwfd = comm_open(SOCK_STREAM,
                                IPPROTO_TCP,	/* protocol */
                                local_addr,
                                0,			/* port */
                                0,			/* blocking */
                                name);
    } else if (type == IPC_UDP_SOCKET) {
        crfd = cwfd = comm_open(SOCK_DGRAM,
                                IPPROTO_UDP,
                                local_addr,
                                0,
                                COMM_NOCLOEXEC,
                                name);
        prfd = pwfd = comm_open(SOCK_DGRAM,
                                IPPROTO_UDP,
                                local_addr,
                                0,
                                0,
                                name);
    } else if (type == IPC_FIFO) {
        debugs(54, 0, "ipcCreate: " << prog << ": use IPC_TCP_SOCKET instead of IP_FIFO on Windows");
        assert(0);
    } else {
        assert(IPC_NONE);
    }

    debugs(54, 3, "ipcCreate: prfd FD " << prfd);
    debugs(54, 3, "ipcCreate: pwfd FD " << pwfd);
    debugs(54, 3, "ipcCreate: crfd FD " << crfd);
    debugs(54, 3, "ipcCreate: cwfd FD " << cwfd);

    if (WIN32_OS_version != _WIN_OS_WINNT) {
        getsockopt(INVALID_SOCKET, SOL_SOCKET, SO_OPENTYPE, (char *) &opt, &optlen);
        opt = opt | SO_SYNCHRONOUS_NONALERT;
        setsockopt(INVALID_SOCKET, SOL_SOCKET, SO_OPENTYPE, (char *) &opt, optlen);
    }

    if (crfd < 0) {
        debugs(54, 0, "ipcCreate: Failed to create child FD.");
        return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
    }

    if (pwfd < 0) {
        debugs(54, 0, "ipcCreate: Failed to create server FD.");
        return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
    }

    if (type == IPC_TCP_SOCKET || type == IPC_UDP_SOCKET) {
        len = sizeof(PS);
        memset(&PS, '\0', len);

        if (getsockname(pwfd, (struct sockaddr *) &PS, &len) < 0) {
            debugs(54, 0, "ipcCreate: getsockname: " << xstrerror());
            return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
        }

        debugs(54, 3, "ipcCreate: FD " << pwfd << " sockaddr " << inet_ntoa(PS.sin_addr) << ":" << ntohs(PS.sin_port));
        len = sizeof(CS);
        memset(&CS, '\0', len);

        if (getsockname(crfd, (struct sockaddr *) &CS, &len) < 0) {
            debugs(54, 0, "ipcCreate: getsockname: " << xstrerror());
            return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
        }

        debugs(54, 3, "ipcCreate: FD " << crfd << " sockaddr " << inet_ntoa(CS.sin_addr) << ":" << ntohs(CS.sin_port));
    }

    if (type == IPC_TCP_SOCKET) {
        if (listen(crfd, 1) < 0) {
            debugs(54, 1, "ipcCreate: listen FD " << crfd << ": " << xstrerror());
            return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
        }

        debugs(54, 3, "ipcCreate: FD " << crfd << " listening...");
    }

    /* flush or else we get dup data if unbuffered_logs is set */
    logsFlush();

    params.type = type;

    params.crfd = crfd;

    params.cwfd = cwfd;

    params.PS = PS;

    params.prog = prog;

    params.args = (char **) args;

    thread = _beginthreadex(NULL, 0, ipc_thread_1, &params, 0, NULL);

    if (thread == 0) {
        debugs(54, 1, "ipcCreate: _beginthread: " << xstrerror());
        return ipcCloseAllFD(prfd, pwfd, crfd, cwfd);
    }

    if (comm_connect_addr(pwfd, &CS) == COMM_ERROR) {
        CloseHandle((HANDLE) thread);
        return ipcCloseAllFD(prfd, pwfd, -1, -1);
    }

    memset(hello_buf, '\0', HELLO_BUF_SZ);
    x = recv(prfd, (void *)hello_buf, HELLO_BUF_SZ - 1, 0);

    if (x < 0) {
        debugs(54, 0, "ipcCreate: PARENT: hello read test failed");
        debugs(54, 0, "--> read: " << xstrerror());
        CloseHandle((HANDLE) thread);
        return ipcCloseAllFD(prfd, pwfd, -1, -1);
    } else if (strcmp(hello_buf, hello_string)) {
        debugs(54, 0, "ipcCreate: PARENT: hello read test failed");
        debugs(54, 0, "--> read returned " << x);
        debugs(54, 0, "--> got '" << rfc1738_escape(hello_buf) << "'");
        CloseHandle((HANDLE) thread);
        return ipcCloseAllFD(prfd, pwfd, -1, -1);
    }

    x = send(pwfd, (const void *)ok_string, strlen(ok_string), 0);

    if (x < 0) {
        debugs(54, 0, "ipcCreate: PARENT: OK write test failed");
        debugs(54, 0, "--> read: " << xstrerror());
        CloseHandle((HANDLE) thread);
        return ipcCloseAllFD(prfd, pwfd, -1, -1);
    }

    memset(hello_buf, '\0', HELLO_BUF_SZ);
    x = recv(prfd, (void *)hello_buf, HELLO_BUF_SZ - 1, 0);

    if (x < 0) {
        debugs(54, 0, "ipcCreate: PARENT: OK read test failed");
        debugs(54, 0, "--> read: " << xstrerror());
        CloseHandle((HANDLE) thread);
        return ipcCloseAllFD(prfd, pwfd, -1, -1);
    } else if (!strcmp(hello_buf, err_string)) {
        debugs(54, 0, "ipcCreate: PARENT: OK read test failed");
        debugs(54, 0, "--> read returned " << x);
        debugs(54, 0, "--> got '" << rfc1738_escape(hello_buf) << "'");
        CloseHandle((HANDLE) thread);
        return ipcCloseAllFD(prfd, pwfd, -1, -1);
    }

    hello_buf[x] = '\0';
    pid = atol(hello_buf);
    commSetTimeout(prfd, -1, NULL, NULL);
    commSetNonBlocking(prfd);
    commSetNonBlocking(pwfd);
    commSetCloseOnExec(prfd);
    commSetCloseOnExec(pwfd);

    if (rfd)
        *rfd = prfd;

    if (wfd)
        *wfd = pwfd;

    fd_table[prfd].flags.ipc = 1;

    fd_table[pwfd].flags.ipc = 1;

    fd_table[crfd].flags.ipc = 1;

    fd_table[cwfd].flags.ipc = 1;

    if (Config.sleep_after_fork) {
        /* XXX emulation of usleep() */
        DWORD sl;
        sl = Config.sleep_after_fork / 1000;

        if (sl == 0)
            sl = 1;

        Sleep(sl);
    }

    if (GetExitCodeThread((HANDLE) thread, &ecode) && ecode == STILL_ACTIVE) {
        if (hIpc)
            *hIpc = (HANDLE) thread;

        return pid;
    } else {
        CloseHandle((HANDLE) thread);
        return ipcCloseAllFD(prfd, pwfd, -1, -1);
    }
}

static int
ipcSend(int cwfd, const char *buf, int len)
{
    int x;

    x = send(cwfd, (const void *)buf, len, 0);

    if (x < 0) {
        debugs(54, 0, "sendto FD " << cwfd << ": " << xstrerror());
        debugs(54, 0, "ipcCreate: CHILD: hello write test failed");
    }

    return x;
}

static unsigned int __stdcall
ipc_thread_1(void *in_params)
{
    int t1, t2, t3, retval = -1;
    int p2c[2] =
        {-1, -1};
    int c2p[2] =
        {-1, -1};
    HANDLE hProcess = NULL, thread = NULL;
    pid_t pid = -1;

    struct thread_params thread_params;
    ssize_t x;
    int tmp_s, fd = -1;
    char *str;
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    long F;
    int prfd_ipc = -1, pwfd_ipc = -1, crfd_ipc = -1, cwfd_ipc = -1;
    char *prog = NULL, *buf1 = NULL;

    struct sockaddr_in CS_ipc, PS_ipc;

    struct ipc_params *params = (struct ipc_params *) in_params;
    int type = params->type;
    int crfd = params->crfd;
    int cwfd = params->cwfd;
    char **args = params->args;

    struct sockaddr_in PS = params->PS;


    buf1 = (char *)xcalloc(1, 8192);
    strcpy(buf1, params->prog);
    prog = strtok(buf1, w_space);

    if ((str = strrchr(prog, '/')))
        prog = ++str;

    if ((str = strrchr(prog, '\\')))
        prog = ++str;

    prog = xstrdup(prog);

    if (type == IPC_TCP_SOCKET) {
        debugs(54, 3, "ipcCreate: calling accept on FD " << crfd);

        if ((fd = accept(crfd, NULL, NULL)) < 0) {
            debugs(54, 0, "ipcCreate: FD " << crfd << " accept: " << xstrerror());
            goto cleanup;
        }

        debugs(54, 3, "ipcCreate: CHILD accepted new FD " << fd);
        comm_close(crfd);
        snprintf(buf1, 8191, "%s CHILD socket", prog);
        fd_open(fd, FD_SOCKET, buf1);
        fd_table[fd].flags.ipc = 1;
        cwfd = crfd = fd;
    } else if (type == IPC_UDP_SOCKET) {
        if (comm_connect_addr(crfd, &PS) == COMM_ERROR)
            goto cleanup;
    }

    x = send(cwfd, (const void *)hello_string, strlen(hello_string) + 1, 0);

    if (x < 0) {
        debugs(54, 0, "sendto FD " << cwfd << ": " << xstrerror());
        debugs(54, 0, "ipcCreate: CHILD: hello write test failed");
        goto cleanup;
    }

    PutEnvironment();
    memset(buf1, '\0', sizeof(buf1));
    x = recv(crfd, (void *)buf1, 8191, 0);

    if (x < 0) {
        debugs(54, 0, "ipcCreate: CHILD: OK read test failed");
        debugs(54, 0, "--> read: " << xstrerror());
        goto cleanup;
    } else if (strcmp(buf1, ok_string)) {
        debugs(54, 0, "ipcCreate: CHILD: OK read test failed");
        debugs(54, 0, "--> read returned " << x);
        debugs(54, 0, "--> got '" << rfc1738_escape(hello_buf) << "'");
        goto cleanup;
    }

    /* assign file descriptors to child process */
    if (_pipe(p2c, 1024, _O_BINARY | _O_NOINHERIT) < 0) {
        debugs(54, 0, "ipcCreate: CHILD: pipe: " << xstrerror());
        ipcSend(cwfd, err_string, strlen(err_string));
        goto cleanup;
    }

    if (_pipe(c2p, 1024, _O_BINARY | _O_NOINHERIT) < 0) {
        debugs(54, 0, "ipcCreate: CHILD: pipe: " << xstrerror());
        ipcSend(cwfd, err_string, strlen(err_string));
        goto cleanup;
    }

    if (type == IPC_UDP_SOCKET) {
        snprintf(buf1, 8192, "%s(%ld) <-> ipc CHILD socket", prog, -1L);
        crfd_ipc = cwfd_ipc = comm_open(SOCK_DGRAM, IPPROTO_UDP, local_addr, 0, 0, buf1);

        if (crfd_ipc < 0) {
            debugs(54, 0, "ipcCreate: CHILD: Failed to create child FD for " << prog << ".");
            ipcSend(cwfd, err_string, strlen(err_string));
            goto cleanup;
        }

        snprintf(buf1, 8192, "%s(%ld) <-> ipc PARENT socket", prog, -1L);
        prfd_ipc = pwfd_ipc = comm_open(SOCK_DGRAM, IPPROTO_UDP, local_addr, 0, 0, buf1);

        if (pwfd_ipc < 0) {
            debugs(54, 0, "ipcCreate: CHILD: Failed to create server FD for " << prog << ".");
            ipcSend(cwfd, err_string, strlen(err_string));
            goto cleanup;
        }

        tmp_s = sizeof(PS_ipc);
        memset(&PS_ipc, '\0', tmp_s);

        if (getsockname(pwfd_ipc, (struct sockaddr *) &PS_ipc, &tmp_s) < 0) {
            debugs(54, 0, "ipcCreate: getsockname: " << xstrerror());
            ipcSend(cwfd, err_string, strlen(err_string));
            goto cleanup;
        }

        debugs(54, 3, "ipcCreate: FD " << pwfd_ipc << " sockaddr " << inet_ntoa(PS_ipc.sin_addr) << ":" << ntohs(PS_ipc.sin_port));

        tmp_s = sizeof(CS_ipc);
        memset(&CS_ipc, '\0', tmp_s);

        if (getsockname(crfd_ipc, (struct sockaddr *) &CS_ipc, &tmp_s) < 0) {
            debugs(54, 0, "ipcCreate: getsockname: " << xstrerror());
            ipcSend(cwfd, err_string, strlen(err_string));
            goto cleanup;
        }

        debugs(54, 3, "ipcCreate: FD " << crfd_ipc << " sockaddr " << inet_ntoa(CS_ipc.sin_addr) << ":" << ntohs(CS_ipc.sin_port));

        if (comm_connect_addr(pwfd_ipc, &CS_ipc) == COMM_ERROR) {
            ipcSend(cwfd, err_string, strlen(err_string));
            goto cleanup;
        }

        fd = crfd;

        if (comm_connect_addr(crfd_ipc, &PS_ipc) == COMM_ERROR) {
            ipcSend(cwfd, err_string, strlen(err_string));
            goto cleanup;
        }
    }				/* IPC_UDP_SOCKET */

    t1 = dup(0);

    t2 = dup(1);

    t3 = dup(2);

    dup2(c2p[0], 0);

    dup2(p2c[1], 1);

    dup2(fileno(debug_log), 2);

    close(c2p[0]);

    close(p2c[1]);

    commUnsetNonBlocking(fd);

    memset(&si, 0, sizeof(STARTUPINFO));

    si.cb = sizeof(STARTUPINFO);

    si.hStdInput = (HANDLE) _get_osfhandle(0);

    si.hStdOutput = (HANDLE) _get_osfhandle(1);

    si.hStdError = (HANDLE) _get_osfhandle(2);

    si.dwFlags = STARTF_USESTDHANDLES;

    /* Make sure all other valid handles are not inerithable */
    for (x = 3; x < Squid_MaxFD; x++) {
        if ((F = _get_osfhandle(x)) == -1)
            continue;

        SetHandleInformation((HANDLE) F, HANDLE_FLAG_INHERIT, 0);
    }

    *buf1 = '\0';
    strcpy(buf1 + 4096, params->prog);
    str = strtok(buf1 + 4096, w_space);

    do {
        strcat(buf1, str);
        strcat(buf1, " ");
    } while ((str = strtok(NULL, w_space)));

    x = 1;

    while (args[x]) {
        strcat(buf1, args[x++]);
        strcat(buf1, " ");
    }

    if (CreateProcess(buf1 + 4096, buf1, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                      NULL, NULL, &si, &pi)) {
        pid = pi.dwProcessId;
        hProcess = pi.hProcess;
    } else {
        pid = -1;
        WIN32_maperror(GetLastError());
        x = errno;
    }

    dup2(t1, 0);
    dup2(t2, 1);
    dup2(t3, 2);
    close(t1);
    close(t2);
    close(t3);

    if (pid == -1) {
        errno = x;
        debugs(54, 0, "ipcCreate: CHILD: " << params->prog << ": " << xstrerror());

        ipcSend(cwfd, err_string, strlen(err_string));
        goto cleanup;
    }

    if (type == IPC_UDP_SOCKET) {
        WSAPROTOCOL_INFO wpi;

        memset(&wpi, 0, sizeof(wpi));

        if (SOCKET_ERROR == WSADuplicateSocket(crfd_ipc, pid, &wpi)) {
            debugs(54, 0, "ipcCreate: CHILD: WSADuplicateSocket: " << xstrerror());

            ipcSend(cwfd, err_string, strlen(err_string));
            goto cleanup;
        }

        x = write(c2p[1], (const char *) &wpi, sizeof(wpi));

        if (x < (ssize_t)sizeof(wpi)) {
            debugs(54, 0, "ipcCreate: CHILD: write FD " << c2p[1] << ": " << xstrerror());
            debugs(54, 0, "ipcCreate: CHILD: " << prog << ": socket exchange failed");

            ipcSend(cwfd, err_string, strlen(err_string));
            goto cleanup;
        }

        x = read(p2c[0], buf1, 8192);

        if (x < 0) {
            debugs(54, 0, "ipcCreate: CHILD: read FD " << p2c[0] << ": " << xstrerror());
            debugs(54, 0, "ipcCreate: CHILD: " << prog << ": socket exchange failed");

            ipcSend(cwfd, err_string, strlen(err_string));
            goto cleanup;
        } else if (strncmp(buf1, ok_string, strlen(ok_string))) {
            debugs(54, 0, "ipcCreate: CHILD: " << prog << ": socket exchange failed");
            debugs(54, 0, "--> read returned " << x);
            buf1[x] = '\0';
            debugs(54, 0, "--> got '" << rfc1738_escape(buf1) << "'");
            ipcSend(cwfd, err_string, strlen(err_string));
            goto cleanup;
        }

        x = write(c2p[1], (const char *) &PS_ipc, sizeof(PS_ipc));

        if (x < (ssize_t)sizeof(PS_ipc)) {
            debugs(54, 0, "ipcCreate: CHILD: write FD " << c2p[1] << ": " << xstrerror());
            debugs(54, 0, "ipcCreate: CHILD: " << prog << ": socket exchange failed");

            ipcSend(cwfd, err_string, strlen(err_string));
            goto cleanup;
        }

        x = read(p2c[0], buf1, 8192);

        if (x < 0) {
            debugs(54, 0, "ipcCreate: CHILD: read FD " << p2c[0] << ": " << xstrerror());
            debugs(54, 0, "ipcCreate: CHILD: " << prog << ": socket exchange failed");

            ipcSend(cwfd, err_string, strlen(err_string));
            goto cleanup;
        } else if (strncmp(buf1, ok_string, strlen(ok_string))) {
            debugs(54, 0, "ipcCreate: CHILD: " << prog << ": socket exchange failed");
            debugs(54, 0, "--> read returned " << x);
            buf1[x] = '\0';
            debugs(54, 0, "--> got '" << rfc1738_escape(buf1) << "'");
            ipcSend(cwfd, err_string, strlen(err_string));
            goto cleanup;
        }

        x = send(pwfd_ipc, (const void *)ok_string, strlen(ok_string), 0);
        x = recv(prfd_ipc, (void *)(buf1 + 200), 8191 - 200, 0);
        assert((size_t) x == strlen(ok_string)
               && !strncmp(ok_string, buf1 + 200, strlen(ok_string)));
    }				/* IPC_UDP_SOCKET */

    snprintf(buf1, 8191, "%s(%ld) CHILD socket", prog, (long int) pid);

    fd_note(fd, buf1);

    if (prfd_ipc != -1) {
        snprintf(buf1, 8191, "%s(%ld) <-> ipc CHILD socket", prog, (long int) pid);
        fd_note(crfd_ipc, buf1);
        snprintf(buf1, 8191, "%s(%ld) <-> ipc PARENT socket", prog, (long int) pid);
        fd_note(prfd_ipc, buf1);
    }

    /* else {                       IPC_TCP_SOCKET */
    /*     commSetNoLinger(fd); */
    /*  } */
    thread_params.prog = prog;

    thread_params.send_fd = cwfd;

    thread_params.pid = pid;

    if ((thread_params.type = type) == IPC_TCP_SOCKET)
        thread_params.rfd = p2c[0];
    else
        thread_params.rfd = prfd_ipc;

    thread =
        (HANDLE) _beginthreadex(NULL, 0, ipc_thread_2, &thread_params, 0, NULL);

    if (!thread) {
        debugs(54, 0, "ipcCreate: CHILD: _beginthreadex: " << xstrerror());
        ipcSend(cwfd, err_string, strlen(err_string));
        goto cleanup;
    }

    snprintf(buf1, 8191, "%ld\n", (long int) pid);

    if (-1 == ipcSend(cwfd, buf1, strlen(buf1)))
        goto cleanup;

        debugs(54, 2, "ipc(" << prog << "," << pid << "): started successfully");

    /* cycle */
    for (;;) {
        x = recv(crfd, (void *)buf1, 8192, 0);

        if (x <= 0) {
            debugs(54, 3, "ipc(" << prog << "," << pid << "): " << x << " bytes received from parent. Exiting...");
            break;
        }

        buf1[x] = '\0';

        if (type == IPC_UDP_SOCKET && !strcmp(buf1, shutdown_string)) {
            debugs(54, 3, "ipc(" << prog << "," << pid << "): request for shutdown received from parent. Exiting...");

            TerminateProcess(hProcess, 0);
            break;
        }

        debugs(54, 5, "ipc(" << prog << "," << pid << "): received from parent: " << rfc1738_escape_unescaped(buf1));

        if (type == IPC_TCP_SOCKET)
            x = write(c2p[1], buf1, x);
        else
            x = send(pwfd_ipc, (const void *)buf1, x, 0);

        if (x <= 0) {
            debugs(54, 3, "ipc(" << prog << "," << pid << "): " << x << " bytes written to " << prog << ". Exiting...");

            break;
        }
    }

    retval = 0;

cleanup:

    if (c2p[1] != -1)
        close(c2p[1]);

    if (fd_table[crfd].flags.open)
        ipcCloseAllFD(-1, -1, crfd, cwfd);

    if (prfd_ipc != -1) {
        send(crfd_ipc, (const void *)shutdown_string, strlen(shutdown_string), 0);
        shutdown(crfd_ipc, SD_BOTH);
        shutdown(prfd_ipc, SD_BOTH);
    }

    ipcCloseAllFD(prfd_ipc, pwfd_ipc, crfd_ipc, cwfd_ipc);

    if (hProcess && WAIT_OBJECT_0 !=
            WaitForSingleObject(hProcess, type == IPC_UDP_SOCKET ? 12000 : 5000)) {

        getCurrentTime();
        debugs(54, 0, "ipc(" << prog << "," << pid << "): WARNING: " << prog <<
               " didn't exit in " << (type == IPC_UDP_SOCKET ? 12 : 5) << " seconds.");

    }

    if (thread && WAIT_OBJECT_0 != WaitForSingleObject(thread, 3000)) {
        getCurrentTime();
        debugs(54, 0, "ipc(" << prog << "," << pid << "): WARNING: ipc_thread_2 didn't exit in 3 seconds.");

    }

    getCurrentTime();

    if (!retval)
        debugs(54, 2, "ipc(" << prog << "," << pid << "): normal exit");

    if (buf1)
        xfree(buf1);

    if (prog)
        xfree(prog);

    if (thread)
        CloseHandle(thread);

    if (hProcess)
        CloseHandle(hProcess);

    if (p2c[0] != -1)
        close(p2c[0]);

    return retval;
}

static unsigned int __stdcall
ipc_thread_2(void *in_params)
{
    int x;

    struct thread_params *params = (struct thread_params *) in_params;
    int type = params->type;
    int rfd = params->rfd;
    int send_fd = params->send_fd;
    char *prog = xstrdup(params->prog);
    pid_t pid = params->pid;
    char *buf2 = (char *)xcalloc(1, 8192);

    for (;;) {
        if (type == IPC_TCP_SOCKET)
            x = read(rfd, buf2, 8192);
        else
            x = recv(rfd, (void *)buf2, 8192, 0);

        if ((x <= 0 && type == IPC_TCP_SOCKET) ||
                (x < 0 && type == IPC_UDP_SOCKET)) {
            debugs(54, 3, "ipc(" << prog << "," << pid << "): " << x << " bytes read from " << prog << ". Exiting...");

            break;
        }

        buf2[x] = '\0';

        if (type == IPC_UDP_SOCKET && !strcmp(buf2, shutdown_string)) {
            debugs(54, 3, "ipc(" << prog << "," << pid << "): request for shutdown received. Exiting...");

            break;
        }

        if (x >= 2) {
            if ((buf2[x - 1] == '\n') && (buf2[x - 2] == '\r')) {
                buf2[x - 2] = '\n';
                buf2[x - 1] = '\0';
                x--;
            }
        }

        debugs(54, 5, "ipc(" << prog << "," << pid << "): received from child : " << rfc1738_escape_unescaped(buf2));

        x = send(send_fd, (const void *)buf2, x, 0);

        if ((x <= 0 && type == IPC_TCP_SOCKET) ||
                (x < 0 && type == IPC_UDP_SOCKET)) {
            debugs(54, 3, "ipc(" << prog << "," << pid << "): " << x << " bytes sent to parent. Exiting...");

            break;
        }
    }

    xfree(prog);
    xfree(buf2);
    return 0;
}
