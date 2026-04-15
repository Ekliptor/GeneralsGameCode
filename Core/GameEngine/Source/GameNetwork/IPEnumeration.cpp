/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/NetworkInit.h"
#include "GameNetwork/networkutil.h"
#include "GameClient/ClientInstance.h"

#include <Utility/socket_compat.h>

#ifndef _WIN32
#include <ifaddrs.h>
#include <net/if.h>
#endif

IPEnumeration::IPEnumeration()
{
	m_IPlist = nullptr;
	m_isWinsockInitialized = false;
}

IPEnumeration::~IPEnumeration()
{
	m_isWinsockInitialized = false;

	EnumeratedIP *ip = m_IPlist;
	while (ip)
	{
		ip = ip->getNext();
		deleteInstance(m_IPlist);
		m_IPlist = ip;
	}
}

EnumeratedIP * IPEnumeration::getAddresses()
{
	if (m_IPlist)
		return m_IPlist;

	if (!NetworkInit::ensureStarted())
		return nullptr;
	m_isWinsockInitialized = true;

	// TheSuperHackers @feature Add one unique local host IP address for each multi client instance.
	if (rts::ClientInstance::isMultiInstance())
	{
		const UnsignedInt id = rts::ClientInstance::getInstanceId();
		addNewIP(
			127,
			(UnsignedByte)(id >> 16),
			(UnsignedByte)(id >> 8),
			(UnsignedByte)(id));
	}

#ifdef _WIN32
	// Windows path: resolve the local host name to its IP list via getaddrinfo.
	// Covers retail hosts where the machine name answers in DNS/NetBIOS.
	char hostname[256];
	if (gethostname(hostname, sizeof(hostname)) != 0)
	{
		DEBUG_LOG(("Failed call to gethostname; WSAGetLastError returned %d", WSAGetLastError()));
		return m_IPlist;
	}
	DEBUG_LOG(("Hostname is '%s'", hostname));

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	struct addrinfo *res = nullptr;
	if (getaddrinfo(hostname, nullptr, &hints, &res) != 0 || res == nullptr)
	{
		DEBUG_LOG(("getaddrinfo failed for hostname '%s'", hostname));
		if (res != nullptr)
			freeaddrinfo(res);
		return m_IPlist;
	}

	for (struct addrinfo *p = res; p != nullptr; p = p->ai_next)
	{
		if (p->ai_family != AF_INET)
			continue;
		const sockaddr_in *sin = reinterpret_cast<const sockaddr_in *>(p->ai_addr);
		const UnsignedInt ip = ntohl(sin->sin_addr.s_addr);
		addNewIP(
			(UnsignedByte)((ip >> 24) & 0xff),
			(UnsignedByte)((ip >> 16) & 0xff),
			(UnsignedByte)((ip >> 8) & 0xff),
			(UnsignedByte)(ip & 0xff));
	}
	freeaddrinfo(res);
#else
	// POSIX path: walk local interfaces via getifaddrs(). Skip loopback; the
	// synthetic 127.x block above already provides multi-instance loopbacks.
	struct ifaddrs *ifaddr = nullptr;
	if (getifaddrs(&ifaddr) != 0 || ifaddr == nullptr)
	{
		DEBUG_LOG(("getifaddrs failed (errno=%d)", errno));
		if (ifaddr != nullptr)
			freeifaddrs(ifaddr);
		return m_IPlist;
	}

	for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
	{
		if (ifa->ifa_addr == nullptr)
			continue;
		if (ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if ((ifa->ifa_flags & IFF_LOOPBACK) != 0)
			continue;
		if ((ifa->ifa_flags & IFF_UP) == 0)
			continue;
		const sockaddr_in *sin = reinterpret_cast<const sockaddr_in *>(ifa->ifa_addr);
		const UnsignedInt ip = ntohl(sin->sin_addr.s_addr);
		addNewIP(
			(UnsignedByte)((ip >> 24) & 0xff),
			(UnsignedByte)((ip >> 16) & 0xff),
			(UnsignedByte)((ip >> 8) & 0xff),
			(UnsignedByte)(ip & 0xff));
	}
	freeifaddrs(ifaddr);
#endif

	return m_IPlist;
}

void IPEnumeration::addNewIP( UnsignedByte a, UnsignedByte b, UnsignedByte c, UnsignedByte d )
{
	EnumeratedIP *newIP = newInstance(EnumeratedIP);

	AsciiString str;
	str.format("%d.%d.%d.%d", (int)a, (int)b, (int)c, (int)d);

	UnsignedInt ip = AssembleIp(a, b, c, d);

	newIP->setIPstring(str);
	newIP->setIP(ip);

	DEBUG_LOG(("IP: 0x%8.8X (%s)", ip, str.str()));

	// Add the IP to the list in ascending order
	if (!m_IPlist)
	{
		m_IPlist = newIP;
		newIP->setNext(nullptr);
	}
	else
	{
		if (newIP->getIP() < m_IPlist->getIP())
		{
			newIP->setNext(m_IPlist);
			m_IPlist = newIP;
		}
		else
		{
			EnumeratedIP *p = m_IPlist;
			while (p->getNext() && p->getNext()->getIP() < newIP->getIP())
			{
				p = p->getNext();
			}
			newIP->setNext(p->getNext());
			p->setNext(newIP);
		}
	}
}

AsciiString IPEnumeration::getMachineName()
{
	if (!NetworkInit::ensureStarted())
		return "";
	m_isWinsockInitialized = true;

	// get the local machine's host name
	char hostname[256];
	if (gethostname(hostname, sizeof(hostname)) != 0)
	{
		DEBUG_LOG(("Failed call to gethostname; WSAGetLastError returned %d", WSAGetLastError()));
		return "";
	}

	return AsciiString(hostname);
}
