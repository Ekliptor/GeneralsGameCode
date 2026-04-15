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

// FILE: GameSpyGameInfo.cpp //////////////////////////////////////////////////////
// GameSpy game setup state info
// Author: Matthew D. Campbell, December 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GameEngine.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/ScoreKeeper.h"
#include "GameClient/Shell.h"
#include "GameClient/GameText.h"
#include "GameNetwork/GameSpy/LocalChatAddress.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpyGameInfo.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/networkutil.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/NAT.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/VictoryConditions.h"

// Singleton ------------------------------------------

GameSpyGameInfo *TheGameSpyGame = nullptr;

// Helper Functions ----------------------------------------

GameSpyGameSlot::GameSpyGameSlot()
{
	GameSlot();
	m_gameSpyLogin.clear();
	m_gameSpyLocale.clear();
	m_profileID = 0;
}

// Helper Functions ----------------------------------------
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#endif

// GameSpyGameInfo ----------------------------------------

GameSpyGameInfo::GameSpyGameInfo()
{
	m_isQM = FALSE;
	m_hasBeenQueried = FALSE;
	for (Int i = 0; i< MAX_SLOTS; ++i)
		setSlotPointer(i, &m_GameSpySlot[i]);

	UnsignedInt localIP;
	if (GetLocalChatConnectionAddress("peerchat.gamespy.com", 6667, localIP))
	{
		localIP = ntohl(localIP); // The IP returned from GetLocalChatConnectionAddress is in network byte order.
		setLocalIP(localIP);
	}
	else
	{
		setLocalIP(0);
	}
	m_server = nullptr;
	m_transport = nullptr;
}

// Misc game-related functionality --------------------

void GameSpyStartGame()
{
	if (TheGameSpyGame)
	{
		int i;

		int numUsers = 0;
		for (i=0; i<MAX_SLOTS; ++i)
		{
			GameSlot *slot = TheGameSpyGame->getSlot(i);
			if (slot && slot->isOccupied())
				numUsers++;
		}

		if (numUsers < 2)
		{
			if (TheGameSpyGame->amIHost())
			{
				UnicodeString text;
				text.format(TheGameText->fetch("LAN:NeedMorePlayers"),numUsers);
				TheGameSpyInfo->addText(text, GSCOLOR_DEFAULT, nullptr);
			}
			return;
		}

		TheGameSpyGame->startGame(0);
	}
}

void GameSpyLaunchGame()
{
	if (TheGameSpyGame)
	{

		// Set up the game network
		AsciiString user;
		AsciiString userList;
		DEBUG_ASSERTCRASH(TheNetwork == nullptr, ("For some reason TheNetwork isn't null at the start of this game.  Better look into that."));

		if (TheNetwork != nullptr) {
			delete TheNetwork;
			TheNetwork = nullptr;
		}

		// Time to initialize TheNetwork for this game.
		TheNetwork = NetworkInterface::createNetwork();
		TheNetwork->init();
		/*
		if (!TheGameSpyGame->amIHost())
			TheNetwork->setLocalAddress((207<<24) | (138<<16) | (47<<8) | 15, 8088);
		else
		*/
		TheNetwork->setLocalAddress(TheGameSpyGame->getLocalIP(), TheNAT->getSlotPort(TheGameSpyGame->getLocalSlotNum()));
		TheNetwork->attachTransport(TheNAT->getTransport());

		user = TheGameSpyInfo->getLocalName();
		for (Int i=0; i<MAX_SLOTS; ++i)
		{
			GameSlot *slot = TheGameSpyGame->getSlot(i);
			if (!slot)
			{
				DEBUG_CRASH(("No GameSlot[%d]!", i));
				delete TheNetwork;
				TheNetwork = nullptr;
				return;
			}

//			UnsignedInt ip = htonl(slot->getIP());
			UnsignedInt ip = slot->getIP();
			AsciiString tmpUserName;
			tmpUserName.translate(slot->getName());
			if (ip)
			{
				/*
				if (i == 1)
				{
					user.format(",%s@207.138.47.15:8088", tmpUserName.str());
				}
				else
				*/
				{
				user.format(",%s@%d.%d.%d.%d:%d", tmpUserName.str(),
					PRINTF_IP_AS_4_INTS(ip),
					TheNAT->getSlotPort(i)
					);
				}
				userList.concat(user);
			}
		}
		userList.trim();

		TheNetwork->parseUserList(TheGameSpyGame);

		// shutdown the top, but do not pop it off the stack
//		TheShell->hideShell();
		// setup the Global Data with the Map and Seed
		TheGlobalData->m_pendingFile = TheGameSpyGame->getMap();

		if (TheGameLogic->isInGame()) {
			TheGameLogic->clearGameData();
		}
		// send a message to the logic for a new game
		GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		msg->appendIntegerArgument(GAME_INTERNET);

		TheGlobalData->m_useFpsLimit = false;

		// Set the random seed
		InitGameLogicRandom( TheGameSpyGame->getSeed() );
		DEBUG_LOG(("InitGameLogicRandom( %d )", TheGameSpyGame->getSeed()));

		if (TheNAT != nullptr) {
			delete TheNAT;
			TheNAT = nullptr;
		}
	}
}

void GameSpyGameInfo::init()
{
	GameInfo::init();

	m_hasBeenQueried = false;
}

void GameSpyGameInfo::resetAccepted()
{
	GameInfo::resetAccepted();

	if (m_hasBeenQueried && amIHost())
	{
		// ANCIENTMUNKEE peerStateChanged(TheGameSpyChat->getPeer());
		m_hasBeenQueried = false;
		DEBUG_LOG(("resetAccepted() called peerStateChange()"));
	}
}

Int GameSpyGameInfo::getLocalSlotNum() const
{
	DEBUG_ASSERTCRASH(m_inGame, ("Looking for local game slot while not in game"));
	if (!m_inGame)
		return -1;

	AsciiString localName = TheGameSpyInfo->getLocalName();

	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = getConstSlot(i);
		if (slot == nullptr) {
			continue;
		}
		if (slot->isPlayer(localName))
			return i;
	}
	return -1;
}

void GameSpyGameInfo::gotGOACall()
{
	DEBUG_LOG(("gotGOACall()"));
	m_hasBeenQueried = true;
}

void GameSpyGameInfo::startGame(Int gameID)
{
	DEBUG_LOG(("GameSpyGameInfo::startGame - game id = %d", gameID));
	DEBUG_ASSERTCRASH(m_transport == nullptr, ("m_transport is not null when it should be"));
	DEBUG_ASSERTCRASH(TheNAT == nullptr, ("TheNAT is not null when it should be"));

	// fill in GS-specific info
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if (m_GameSpySlot[i].isHuman())
		{
			AsciiString gsName;
			gsName.translate( m_GameSpySlot[i].getName() );
			m_GameSpySlot[i].setLoginName( gsName );

			PlayerInfoMap *pInfoMap = TheGameSpyInfo->getPlayerInfoMap();
			PlayerInfoMap::iterator it = pInfoMap->find(gsName);
			if (it != pInfoMap->end())
			{
				m_GameSpySlot[i].setProfileID(it->second.m_profileID);
				m_GameSpySlot[i].setLocale(it->second.m_locale);
			}
			else
			{
				DEBUG_CRASH(("No player info for %s", gsName.str()));
			}
		}
	}

	if (TheNAT != nullptr) {
		delete TheNAT;
		TheNAT = nullptr;
	}
	TheNAT = NEW NAT();
	TheNAT->attachSlotList(m_slot, getLocalSlotNum(), m_localIP);
	TheNAT->establishConnectionPaths();
}

AsciiString GameSpyGameInfo::generateGameResultsPacket()
{
	Int i;
	Int endFrame = TheVictoryConditions->getEndFrame();
	Int localSlotNum = getLocalSlotNum();
	//GameSlot *localSlot = getSlot(localSlotNum);
	Bool sawGameEnd = (endFrame > 0);// && localSlot->lastFrameInGame() <= endFrame);
	Int winningTeam = -1;
	Int numPlayers = 0;
	Int numTeamsAtGameEnd = 0;
	Int lastTeamAtGameEnd = -1;
	for (i=0; i<MAX_SLOTS; ++i)
	{
		AsciiString playerName;
		playerName.format("player%d", i);
		Player *p = ThePlayerList->findPlayerWithNameKey(NAMEKEY(playerName));
		if (p)
		{
			++numPlayers;
			if (TheVictoryConditions->hasAchievedVictory(p))
			{
				winningTeam = getSlot(i)->getTeamNumber();
			}

			// check if he lasted
			GameSlot *slot = getSlot(i);
			if (!slot->disconnected())
			{
				if (slot->getTeamNumber() != lastTeamAtGameEnd || numTeamsAtGameEnd == 0)
				{
					lastTeamAtGameEnd = slot->getTeamNumber();
					++numTeamsAtGameEnd;
				}
			}
		}
	}

	AsciiString results;
	results.format("seed=%d,slotNum=%d,sawDesync=%d,sawGameEnd=%d,winningTeam=%d,disconEnd=%d,duration=%d,numPlayers=%d,isQM=%d",
		getSeed(), localSlotNum, TheNetwork->sawCRCMismatch(), sawGameEnd, winningTeam, (numTeamsAtGameEnd != 0),
		endFrame, numPlayers, m_isQM);

	Int playerID = 0;
	for (i=0; i<MAX_SLOTS; ++i)
	{
		AsciiString playerName;
		playerName.format("player%d", i);
		Player *p = ThePlayerList->findPlayerWithNameKey(NAMEKEY(playerName));
		if (p)
		{
			GameSpyGameSlot *slot = &(m_GameSpySlot[i]);
			ScoreKeeper *keeper = p->getScoreKeeper();
			AsciiString playerName = slot->getLoginName();
			Int gsPlayerID = slot->getProfileID();
			AsciiString locale = slot->getLocale();
			Int fps = TheNetwork->getAverageFPS();
			Int unitsKilled = keeper->getTotalUnitsDestroyed();
			Int unitsLost = keeper->getTotalUnitsLost();
			Int unitsBuilt = keeper->getTotalUnitsBuilt();
			Int buildingsKilled = keeper->getTotalBuildingsDestroyed();
			Int buildingsLost = keeper->getTotalBuildingsLost();
			Int buildingsBuilt = keeper->getTotalBuildingsBuilt();
			Int earnings = keeper->getTotalMoneyEarned();
			Int techCaptured = keeper->getTotalTechBuildingsCaptured();
			Bool disconnected = slot->disconnected();

			AsciiString playerStr;
			playerStr.format(",player%d=%s,playerID%d=%d,locale%d=%s",
				playerID, playerName.str(), playerID, gsPlayerID, playerID, locale.str());
			results.concat(playerStr);
			playerStr.format(",unitsKilled%d=%d,unitsLost%d=%d,unitsBuilt%d=%d",
				playerID, unitsKilled, playerID, unitsLost, playerID, unitsBuilt);
			results.concat(playerStr);
			playerStr.format(",buildingsKilled%d=%d,buildingsLost%d=%d,buildingsBuilt%d=%d",
				playerID, buildingsKilled, playerID, buildingsLost, playerID, buildingsBuilt);
			results.concat(playerStr);
			playerStr.format(",fps%d=%d,cash%d=%d,capturedTech%d=%d,discon%d=%d",
				playerID, fps, playerID, earnings, playerID, techCaptured, playerID, disconnected);
			results.concat(playerStr);

			++playerID;
		}
	}

	// Add a trailing size value (so the server can ensure it got the entire packet)
	int resultsLen = results.getLength()+10;
	AsciiString tail;
	tail.format("%10.10d", resultsLen);
	results.concat(tail);

	return results;
}

