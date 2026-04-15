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

// FTP.cpp : Phase 4 libcurl-backed rewrite of the original hand-rolled
// Winsock FTP client. Preserves the Cftp public API consumed by CDownload.
// Download semantics:
//   - ConnectToServer/LoginToServer stash credentials; no socket work yet.
//   - FindFile opens a short-lived easy handle with CURLOPT_NOBODY to read
//     CURLINFO_CONTENT_LENGTH_DOWNLOAD_T.
//   - GetNextFileBlock sets up an active easy handle attached to a multi
//     handle, calls curl_multi_perform() each pump, and reports progress
//     via the write/progress callbacks. Finishes when curl_multi_info_read
//     yields CURLMSG_DONE.
//   - Resume uses CURLOPT_RESUME_FROM_LARGE and the temp-file size on disk.

#include <atomic>

#include "DownloadDebug.h"
#include "Download.h"
#include "stringex.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>

#include <curl/curl.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define FTP_STAT_STRUCT struct _stat
#define ftp_stat _stat
#else
#include <unistd.h>
#include <sys/types.h>
#define FTP_STAT_STRUCT struct stat
#define ftp_stat stat
#endif

namespace
{
	std::atomic<bool> s_curlGlobalInitDone{false};

	void ensureCurlGlobalInit()
	{
		bool expected = false;
		if (s_curlGlobalInitDone.compare_exchange_strong(expected, true))
		{
			curl_global_init(CURL_GLOBAL_DEFAULT);
		}
	}

	void buildFtpUrl(char *out, size_t outLen,
	                  const char *server, const char *user, const char *pass,
	                  const char *remotePath)
	{
		// curl URL-encodes credentials for us via CURLOPT_USERNAME/PASSWORD,
		// so we just need server + path here.
		(void)user;
		(void)pass;
		// Normalize backslashes in the remote path (Windows-ism).
		char path[512];
		size_t i = 0;
		for (; remotePath[i] != '\0' && i + 1 < sizeof(path); ++i)
		{
			path[i] = (remotePath[i] == '\\') ? '/' : remotePath[i];
		}
		path[i] = '\0';
		if (path[0] != '/')
		{
			snprintf(out, outLen, "ftp://%s/%s", server, path);
		}
		else
		{
			snprintf(out, outLen, "ftp://%s%s", server, path);
		}
	}

	size_t writeToFileCallback(char *buffer, size_t size, size_t nmemb, void *userdata)
	{
		FILE *fp = static_cast<FILE *>(userdata);
		if (fp == nullptr)
			return 0;
		return fwrite(buffer, size, nmemb, fp);
	}

	size_t discardCallback(char * /*buffer*/, size_t size, size_t nmemb, void * /*userdata*/)
	{
		return size * nmemb;
	}
}

// Libcurl progress callback. Defined out-of-line so its curl_off_t signature
// matches curl's typedef exactly. Uses Cftp::updateProgress() to mutate the
// private counters — avoids leaking <curl/curl.h> into ftp.h.
static int xferInfoCallback(void *userdata,
                             curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t /*ultotal*/, curl_off_t /*ulnow*/)
{
	Cftp *ftp = static_cast<Cftp *>(userdata);
	if (ftp == nullptr)
		return 0;
	if (dltotal > 0)
	{
		ftp->m_iFileSize = static_cast<int>(dltotal) + ftp->m_iFilePos;
	}
	ftp->m_iBytesRead = static_cast<int>(dlnow) + ftp->m_iFilePos;
	return 0;
}

Cftp::Cftp()
{
	ensureCurlGlobalInit();

	m_iCommandSocket = 0;
	m_iFilePos = 0;
	m_iBytesRead = 0;
	m_iFileSize = 0;
	m_iStatus = 0;
	m_pfLocalFile = nullptr;
	m_curlMulti = nullptr;
	m_curlEasy = nullptr;
	m_szRemoteFilePath[0] = '\0';
	m_szRemoteFileName[0] = '\0';
	m_szLocalFilePath[0] = '\0';
	m_szLocalFileName[0] = '\0';
	m_szServerName[0] = '\0';
	m_szUserName[0] = '\0';
	m_szPassword[0] = '\0';
}

Cftp::~Cftp()
{
	DisconnectFromServer();
}

HRESULT Cftp::ConnectToServer(LPCSTR szServerName)
{
	if (szServerName == nullptr)
		return FTP_FAILED;
	strlcpy(m_szServerName, szServerName, sizeof(m_szServerName));
	m_iCommandSocket = 1; // legacy "connected" signal for Download.cpp
	return FTP_SUCCEEDED;
}

HRESULT Cftp::DisconnectFromServer()
{
	if (m_curlEasy != nullptr && m_curlMulti != nullptr)
	{
		curl_multi_remove_handle(static_cast<CURLM *>(m_curlMulti),
		                         static_cast<CURL *>(m_curlEasy));
	}
	if (m_curlEasy != nullptr)
	{
		curl_easy_cleanup(static_cast<CURL *>(m_curlEasy));
		m_curlEasy = nullptr;
	}
	if (m_curlMulti != nullptr)
	{
		curl_multi_cleanup(static_cast<CURLM *>(m_curlMulti));
		m_curlMulti = nullptr;
	}
	if (m_pfLocalFile != nullptr)
	{
		fclose(m_pfLocalFile);
		m_pfLocalFile = nullptr;
	}
	m_iCommandSocket = 0;
	m_iFilePos = 0;
	m_iBytesRead = 0;
	m_iFileSize = 0;
	return FTP_SUCCEEDED;
}

HRESULT Cftp::LoginToServer(LPCSTR szUserName, LPCSTR szPassword)
{
	if (szUserName == nullptr || szPassword == nullptr)
		return FTP_FAILED;
	strlcpy(m_szUserName, szUserName, sizeof(m_szUserName));
	strlcpy(m_szPassword, szPassword, sizeof(m_szPassword));
	return FTP_SUCCEEDED;
}

HRESULT Cftp::LogoffFromServer()
{
	return FTP_SUCCEEDED;
}

HRESULT Cftp::FindFile(LPCSTR szRemoteFileName, int *piSize)
{
	if (szRemoteFileName == nullptr || piSize == nullptr)
		return FTP_FAILED;
	strlcpy(m_szRemoteFileName, szRemoteFileName, sizeof(m_szRemoteFileName));

	char url[512];
	buildFtpUrl(url, sizeof(url), m_szServerName, m_szUserName, m_szPassword, szRemoteFileName);

	CURL *easy = curl_easy_init();
	if (easy == nullptr)
		return FTP_FAILED;

	curl_easy_setopt(easy, CURLOPT_URL, url);
	if (m_szUserName[0] != '\0')
	{
		curl_easy_setopt(easy, CURLOPT_USERNAME, m_szUserName);
		curl_easy_setopt(easy, CURLOPT_PASSWORD, m_szPassword);
	}
	curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
	curl_easy_setopt(easy, CURLOPT_HEADER, 0L);
	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, discardCallback);
	curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(easy, CURLOPT_TIMEOUT, 30L);

	const CURLcode rc = curl_easy_perform(easy);
	HRESULT result = FTP_FAILED;
	if (rc == CURLE_OK)
	{
		curl_off_t len = -1;
		curl_easy_getinfo(easy, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &len);
		if (len > 0)
		{
			*piSize = static_cast<int>(len);
			m_iFileSize = *piSize;
			result = FTP_SUCCEEDED;
		}
	}
	curl_easy_cleanup(easy);
	return result;
}

HRESULT Cftp::FileRecoveryPosition(LPCSTR szLocalFileName, LPCSTR /*szRegistryRoot*/)
{
	if (szLocalFileName == nullptr)
	{
		m_iFilePos = 0;
		return FTP_FAILED;
	}

	char downloadfilename[512];
	GetDownloadFilename(szLocalFileName, downloadfilename, sizeof(downloadfilename));

	FTP_STAT_STRUCT st;
	if (ftp_stat(downloadfilename, &st) == 0 && st.st_size > 0 && st.st_size < m_iFileSize)
	{
		m_iFilePos = static_cast<int>(st.st_size);
		return FTP_SUCCEEDED;
	}
	m_iFilePos = 0;
	return FTP_SUCCEEDED;
}

HRESULT Cftp::GetNextFileBlock(LPCSTR szLocalFileName, int *piTotalRead)
{
	if (szLocalFileName == nullptr || piTotalRead == nullptr)
		return FTP_FAILED;
	strlcpy(m_szLocalFileName, szLocalFileName, sizeof(m_szLocalFileName));

	// First call for this file: set up the multi/easy handles and open the
	// temp output file for append/write.
	if (m_curlMulti == nullptr)
	{
		char downloadfilename[512];
		GetDownloadFilename(szLocalFileName, downloadfilename, sizeof(downloadfilename));

		m_pfLocalFile = fopen(downloadfilename, (m_iFilePos > 0) ? "ab" : "wb");
		if (m_pfLocalFile == nullptr)
		{
			DEBUG_LOG(("Cftp::GetNextFileBlock: could not open %s for write", downloadfilename));
			return FTP_FAILED;
		}

		char url[512];
		buildFtpUrl(url, sizeof(url), m_szServerName, m_szUserName, m_szPassword, m_szRemoteFileName);

		CURL *easy = curl_easy_init();
		CURLM *multi = curl_multi_init();
		if (easy == nullptr || multi == nullptr)
		{
			if (easy) curl_easy_cleanup(easy);
			if (multi) curl_multi_cleanup(multi);
			fclose(m_pfLocalFile);
			m_pfLocalFile = nullptr;
			return FTP_FAILED;
		}

		curl_easy_setopt(easy, CURLOPT_URL, url);
		if (m_szUserName[0] != '\0')
		{
			curl_easy_setopt(easy, CURLOPT_USERNAME, m_szUserName);
			curl_easy_setopt(easy, CURLOPT_PASSWORD, m_szPassword);
		}
		curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, writeToFileCallback);
		curl_easy_setopt(easy, CURLOPT_WRITEDATA, m_pfLocalFile);
		curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, xferInfoCallback);
		curl_easy_setopt(easy, CURLOPT_XFERINFODATA, this);
		curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 15L);
		if (m_iFilePos > 0)
		{
			curl_easy_setopt(easy, CURLOPT_RESUME_FROM_LARGE,
			                 static_cast<curl_off_t>(m_iFilePos));
		}

		if (curl_multi_add_handle(multi, easy) != CURLM_OK)
		{
			curl_easy_cleanup(easy);
			curl_multi_cleanup(multi);
			fclose(m_pfLocalFile);
			m_pfLocalFile = nullptr;
			return FTP_FAILED;
		}

		m_curlEasy = easy;
		m_curlMulti = multi;
		m_iBytesRead = m_iFilePos;
	}

	int stillRunning = 0;
	const CURLMcode pc = curl_multi_perform(static_cast<CURLM *>(m_curlMulti), &stillRunning);
	if (pc != CURLM_OK)
	{
		DisconnectFromServer();
		return FTP_FAILED;
	}

	*piTotalRead = m_iBytesRead;

	if (stillRunning == 0)
	{
		// Check final status.
		int msgsInQueue = 0;
		CURLcode transferResult = CURLE_OK;
		while (CURLMsg *msg = curl_multi_info_read(static_cast<CURLM *>(m_curlMulti), &msgsInQueue))
		{
			if (msg->msg == CURLMSG_DONE)
			{
				transferResult = msg->data.result;
			}
		}

		curl_multi_remove_handle(static_cast<CURLM *>(m_curlMulti),
		                         static_cast<CURL *>(m_curlEasy));
		curl_easy_cleanup(static_cast<CURL *>(m_curlEasy));
		curl_multi_cleanup(static_cast<CURLM *>(m_curlMulti));
		m_curlEasy = nullptr;
		m_curlMulti = nullptr;

		if (m_pfLocalFile != nullptr)
		{
			fclose(m_pfLocalFile);
			m_pfLocalFile = nullptr;
		}

		if (transferResult != CURLE_OK)
		{
			DEBUG_LOG(("Cftp::GetNextFileBlock: curl failed (%d: %s)",
				static_cast<int>(transferResult), curl_easy_strerror(transferResult)));
			return FTP_FAILED;
		}

		// Move the tempfile into place at m_szLocalFileName.
		char downloadfilename[512];
		GetDownloadFilename(m_szLocalFileName, downloadfilename, sizeof(downloadfilename));
		(void)rename(downloadfilename, m_szLocalFileName);

		return FTP_SUCCEEDED;
	}

	return FTP_TRYING;
}

void Cftp::GetDownloadFilename(const char *localname, char *downloadname, size_t downloadname_size)
{
	char *name = strdup(localname);
	char *s = name;
	while (*s)
	{
		if (*s == '\\' || *s == '.' || *s == ' ')
			*s = '_';
		++s;
	}
	snprintf(downloadname, downloadname_size, "download\\%s_%d.tmp", name, m_iFileSize);
	free(name);
}
