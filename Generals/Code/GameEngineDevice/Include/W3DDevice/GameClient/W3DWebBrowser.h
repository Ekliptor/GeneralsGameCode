/*
**	Command & Conquer Generals(tm)
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

///// W3DWebBrowser.h ////////////////////////
// July 2002, Bryan Cleveland
// Phase 3: embedded IE/COM browser removed. Class remains as a no-op shell
// so the two legacy UI call sites (WOLLoginMenu, WOLLadderScreen) still link.

#pragma once

#include "GameNetwork/WOLBrowser/WebBrowser.h"

class GameWindow;

class W3DWebBrowser : public WebBrowser
{
public:
	W3DWebBrowser() = default;

	virtual Bool createBrowserWindow(const char * /*tag*/, GameWindow * /*win*/) override { return TRUE; }
	virtual void closeBrowserWindow(GameWindow * /*win*/) override {}
};
