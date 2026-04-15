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

// ftp.h : Declaration of the Cftp
//
// Phase 4: the internals of Cftp are now a libcurl multi-handle state
// machine. The public API is unchanged so CDownload (Download.{h,cpp})
// and Core/Tools/PATCHGET don't need to know anything changed.

#pragma once

#include <cstddef>
#include <cstdio>
#include <Utility/hresult_compat.h>
#include <Utility/stdio_adapter.h>

#include "WWDownload/ftpdefs.h"

// Temporary download file name
#define FTP_TEMPFILENAME	"..\\__~DOWN_L~D"


/////////////////////////////////////////////////////////////////////////////
// Cftp
class Cftp
{
public:
	// Progress counters — mutated by the libcurl write/xferinfo callbacks in
	// FTP.cpp. Public so the callbacks don't need to be friends of Cftp.
	int		m_iFilePos;									// Byte offset into file
	int		m_iBytesRead;								// Number of bytes downloaded
	int		m_iFileSize;								// Total size of the file

private:
	friend class CDownload;

	// Legacy signal used by Download.cpp:PumpMessages() to decide whether
	// the command channel is still open. With libcurl we just track whether
	// we have an active easy handle attached to the multi.
	int		m_iCommandSocket;
	char	m_szRemoteFilePath[128];
	char	m_szRemoteFileName[128];
	char	m_szLocalFilePath[128];
	char	m_szLocalFileName[256];
	char	m_szServerName[128];
	char	m_szUserName[128];
	char	m_szPassword[128];
	FILE *	m_pfLocalFile;
	int		m_iStatus;

	// libcurl multi/easy handles; void* so ftp.h doesn't pull curl/curl.h
	// into every TU that includes Cftp.
	void *	m_curlMulti;
	void *	m_curlEasy;

	// Convert a local filename into a temp filename to download into
	void	GetDownloadFilename(const char *localname, char *downloadname, size_t downloadname_size);

public:
	Cftp();
	virtual ~Cftp();

public:
	HRESULT ConnectToServer(LPCSTR szServerName);
	HRESULT DisconnectFromServer();

	HRESULT LoginToServer( LPCSTR szUserName, LPCSTR szPassword );
	HRESULT LogoffFromServer();

	HRESULT FindFile( LPCSTR szRemoteFileName, int * piSize );

	HRESULT FileRecoveryPosition( LPCSTR szLocalFileName, LPCSTR szRegistryRoot );
	HRESULT RestartFrom( int i ) { m_iFilePos = i; return FTP_SUCCEEDED;  };

	HRESULT GetNextFileBlock( LPCSTR szLocalFileName, int * piTotalRead );
};
