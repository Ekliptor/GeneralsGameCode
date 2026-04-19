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

#include "PreRTS.h"

#include "GameNetwork/GameSpy/LocalChatAddress.h"

#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/NetworkInit.h"
#include "GameNetwork/networkutil.h"

#include <Utility/socket_compat.h>

Bool GetLocalChatConnectionAddress(AsciiString serverName,
                                   UnsignedShort serverPort,
                                   UnsignedInt& localIP)
{
	localIP = 0;

	if (!NetworkInit::ensureStarted())
		return FALSE;

	UnsignedInt serverHostOrderIP = 0;
	if (!resolveHostIPv4(serverName.str(), serverHostOrderIP))
	{
		DEBUG_LOG(("GetLocalChatConnectionAddress: could not resolve %s", serverName.str()));
	}

	// UDP connect() does not send any packets but causes the kernel to pick
	// a source address based on its routing table. getsockname() then reports
	// the address that would be used on an outgoing send.
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == INVALID_SOCKET)
	{
		DEBUG_LOG(("GetLocalChatConnectionAddress: socket() failed"));
	}
	else
	{
		sockaddr_in to;
		memset(&to, 0, sizeof(to));
		to.sin_family = AF_INET;
		to.sin_port = htons(serverPort ? serverPort : 80);
		to.sin_addr.s_addr = htonl(serverHostOrderIP ? serverHostOrderIP : INADDR_LOOPBACK);

		if (connect(fd, reinterpret_cast<const sockaddr *>(&to), sizeof(to)) == 0)
		{
			sockaddr_in local;
			memset(&local, 0, sizeof(local));
			socklen_t locallen = sizeof(local);
			if (getsockname(fd, reinterpret_cast<sockaddr *>(&local), &locallen) == 0)
			{
				// Returned in network byte order to match the pre-Phase 4 SNMP
				// implementation's contract (callers ntohl() the value).
				localIP = local.sin_addr.s_addr;
			}
		}
		closesocket(fd);
	}

	if (localIP != 0 && ntohl(localIP) != INADDR_LOOPBACK)
	{
		DEBUG_LOG(("GetLocalChatConnectionAddress: resolved local IP 0x%08x (network order)", localIP));
		return TRUE;
	}

	// Fallback: first non-loopback IP from IPEnumeration (which returns host
	// order; convert back to network order for the caller).
	IPEnumeration enumer;
	const EnumeratedIP *ip = enumer.getAddresses();
	while (ip != nullptr)
	{
		const UnsignedInt candidate = ip->getIP();
		if (candidate != 0 && (candidate & 0xff000000) != 0x7f000000)
		{
			localIP = htonl(candidate);
			DEBUG_LOG(("GetLocalChatConnectionAddress: fell back to IP 0x%08x (network order)", localIP));
			return TRUE;
		}
		ip = const_cast<EnumeratedIP *>(ip)->getNext();
	}

	return FALSE;
}
