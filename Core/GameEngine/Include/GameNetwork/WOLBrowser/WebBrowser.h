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

// WebBrowser.h //////////////////////////////////////////////////////////////
//
// Phase 3 — Plain abstract interface. Retail used an embedded IE ActiveX
// control routed through FEBDispatch/ATL to host in-game community screens
// (TOS, Message Board). Those services are dead; the class survives as a
// no-op shell so its two UI callers (WOLLoginMenu, WOLLadderScreen) still
// link. External URL routing, if any returns, should use SDL_OpenURL.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/SubsystemInterface.h"
#include "Common/GameMemory.h"
#include <Lib/BaseType.h>

class GameWindow;

class WebBrowserURL : public MemoryPoolObject
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( WebBrowserURL, "WebBrowserURL" )

public:

	WebBrowserURL();
	// virtual destructor prototype defined by memory pool object

	const FieldParse *getFieldParse() const { return m_URLFieldParseTable; }

	AsciiString m_tag;
	AsciiString m_url;

	WebBrowserURL *m_next;

	static const FieldParse m_URLFieldParseTable[];		///< the parse table for INI definition
};


class WebBrowser : public SubsystemInterface
{
public:
	WebBrowser();
	virtual ~WebBrowser() override;

	virtual void init() override;
	virtual void reset() override;
	virtual void update() override;

	// Concrete subclasses no-op these post-Phase-3. Kept virtual so a future
	// cross-platform in-game browser (CEF, Ultralight) can slot in.
	virtual Bool createBrowserWindow(const char *tag, GameWindow *win) = 0;
	virtual void closeBrowserWindow(GameWindow *win) = 0;

	WebBrowserURL *makeNewURL(AsciiString tag);
	WebBrowserURL *findURL(AsciiString tag);

protected:
	WebBrowser(const WebBrowser&) = delete;
	WebBrowser& operator=(const WebBrowser&) = delete;

	WebBrowserURL *m_urlList;
};

extern WebBrowser *TheWebBrowser;
