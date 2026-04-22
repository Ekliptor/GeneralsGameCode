/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Platform socket-API shim. On Windows this is a thin Winsock include; on
// POSIX it maps Winsock error codes and closesocket/ioctlsocket onto their
// BSD-socket equivalents so the same source compiles unchanged.

#pragma once

#ifdef _WIN32

#include <winsock.h>
#include <ws2tcpip.h>

#else // POSIX

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif

#ifndef WSAEWOULDBLOCK
#define WSAEWOULDBLOCK EWOULDBLOCK
#endif

#ifndef WSAEINPROGRESS
#define WSAEINPROGRESS EINPROGRESS
#endif

#ifndef WSAEINTR
#define WSAEINTR EINTR
#endif

#ifndef WSAECONNREFUSED
#define WSAECONNREFUSED ECONNREFUSED
#endif

#ifndef WSAEINVAL
#define WSAEINVAL EINVAL
#endif

#ifndef WSAEISCONN
#define WSAEISCONN EISCONN
#endif

#ifndef WSAEADDRINUSE
#define WSAEADDRINUSE EADDRINUSE
#endif

#ifndef WSAEADDRNOTAVAIL
#define WSAEADDRNOTAVAIL EADDRNOTAVAIL
#endif

#ifndef WSAENOTSOCK
#define WSAENOTSOCK ENOTSOCK
#endif

#ifndef WSAETIMEDOUT
#define WSAETIMEDOUT ETIMEDOUT
#endif

#ifndef WSAEALREADY
#define WSAEALREADY EALREADY
#endif

#ifndef WSAENETUNREACH
#define WSAENETUNREACH ENETUNREACH
#endif

#ifndef WSAEHOSTUNREACH
#define WSAEHOSTUNREACH EHOSTUNREACH
#endif

#ifndef WSAECONNRESET
#define WSAECONNRESET ECONNRESET
#endif

#ifndef WSAECONNABORTED
#define WSAECONNABORTED ECONNABORTED
#endif

#ifndef WSAENOBUFS
#define WSAENOBUFS ENOBUFS
#endif

#ifndef WSAEMSGSIZE
#define WSAEMSGSIZE EMSGSIZE
#endif

// gamespy's gsplatformsocket.h does `#define closesocket close` on Unix.
// Undefine so our inline wrapper below is not rewritten into a redeclaration
// of the POSIX close().
#ifdef closesocket
#undef closesocket
#endif
#ifdef ioctlsocket
#undef ioctlsocket
#endif
#ifdef WSAGetLastError
#undef WSAGetLastError
#endif
static inline int closesocket(int fd) { return ::close(fd); }
static inline int ioctlsocket(int fd, long cmd, unsigned long *argp)
{
    return ::ioctl(fd, cmd, argp);
}
static inline int WSAGetLastError() { return errno; }

#endif // _WIN32
