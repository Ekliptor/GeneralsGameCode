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

// WebBrowser.cpp ////////////////////////////////////////////////////////////
//
// Phase 3 — Plain abstract interface (ATL/COM stripped). See WebBrowser.h.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/WOLBrowser/WebBrowser.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Display.h"

WebBrowser *TheWebBrowser = nullptr;

WebBrowser::WebBrowser() : m_urlList(nullptr)
{
	DEBUG_LOG(("Instantiating WebBrowser stub"));
}

WebBrowser::~WebBrowser()
{
	if (this == TheWebBrowser)
		TheWebBrowser = nullptr;

	WebBrowserURL *url = m_urlList;
	while (url != nullptr)
	{
		WebBrowserURL *temp = url;
		url = url->m_next;
		deleteInstance(temp);
	}
	m_urlList = nullptr;
}

//-------------------------------------------------------------------------------------------------
/** The INI data fields for Webpage URL's */
//-------------------------------------------------------------------------------------------------
const FieldParse WebBrowserURL::m_URLFieldParseTable[] =
{
	{ "URL", INI::parseAsciiString, nullptr, offsetof( WebBrowserURL, m_url ) },
	{ nullptr, nullptr, nullptr, 0 },
};

WebBrowserURL::WebBrowserURL()
	: m_next(nullptr)
{
	m_tag.clear();
	m_url.clear();
}

WebBrowserURL::~WebBrowserURL() = default;

void WebBrowser::init()
{
	m_urlList = nullptr;
	INI ini;
	ini.loadFileDirectory("Data\\INI\\Webpages", INI_LOAD_OVERWRITE, nullptr);
}

void WebBrowser::reset() {}
void WebBrowser::update() {}

WebBrowserURL *WebBrowser::findURL(AsciiString tag)
{
	WebBrowserURL *r = m_urlList;
	while (r != nullptr && tag.compareNoCase(r->m_tag.str()))
		r = r->m_next;
	return r;
}

WebBrowserURL *WebBrowser::makeNewURL(AsciiString tag)
{
	WebBrowserURL *u = newInstance(WebBrowserURL);
	u->m_tag = tag;
	u->m_next = m_urlList;
	m_urlList = u;
	return u;
}
