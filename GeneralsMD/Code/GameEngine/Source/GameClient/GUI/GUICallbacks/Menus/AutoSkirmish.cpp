// AutoSkirmish.cpp — driver for the `-skirmish [<map>]` CLI flag.
// Replicates the SkirmishGameOptions menu's "Start" button programmatically:
// constructs TheSkirmishGameInfo, configures slot 0 (local human) + slot 1
// (Easy AI), picks the map, and appends MSG_NEW_GAME(GAME_SKIRMISH). Used
// for in-game terrain / lighting / unit verification with screenshots when
// the menu UI can't be hand-driven.

#include "PreRTS.h"

#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/SkirmishPreferences.h"
#include "GameLogic/GameLogic.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GameInfo.h"

#include <cstdio>

extern SkirmishGameInfo *TheSkirmishGameInfo;

Bool TryAutoSkirmishLaunch(const AsciiString& explicitMap)
{
	// One-shot: caller clears m_autoSkirmish on success so this only fires
	// once per process.
	if (TheGameLogic && TheGameLogic->isInGame())
		return TRUE;
	if (TheMapCache == nullptr || TheMessageStream == nullptr)
		return FALSE;

	if (TheSkirmishGameInfo == nullptr)
		TheSkirmishGameInfo = NEW SkirmishGameInfo;

	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	const Int localIP = TheSkirmishGameInfo->getSlot(0)->getIP();
	TheSkirmishGameInfo->setLocalIP(localIP);
	TheSkirmishGameInfo->enterGame();

	SkirmishPreferences prefs;

	GameSlot localSlot;
	localSlot.setName(prefs.getUserName());
	localSlot.setState(SLOT_PLAYER, prefs.getUserName());
	localSlot.setColor(prefs.getPreferredColor());

	Int factionIdx = prefs.getPreferredFaction();
	const AsciiString& factionOverride = TheGlobalData->m_autoSkirmishFaction;
	if (!factionOverride.isEmpty() && ThePlayerTemplateStore != NULL)
	{
		const NameKeyType key = TheNameKeyGenerator->nameToKey(factionOverride);
		const PlayerTemplate* tmpl = ThePlayerTemplateStore->findPlayerTemplate(key);
		if (tmpl != NULL && !tmpl->getStartingBuilding().isEmpty())
		{
			factionIdx = ThePlayerTemplateStore->getTemplateNumByName(factionOverride);
			std::fprintf(stderr,
				"[AutoSkirmish] -skirmishFaction override: %s -> templateIdx=%d\n",
				factionOverride.str(), factionIdx);
		}
		else
		{
			std::fprintf(stderr,
				"[AutoSkirmish] -skirmishFaction '%s' not found or has no starting building; "
				"falling back to SkirmishPreferences default (idx=%d)\n",
				factionOverride.str(), factionIdx);
		}
	}
	localSlot.setPlayerTemplate(factionIdx);
	TheSkirmishGameInfo->setSlot(0, localSlot);

	GameSlot aiSlot;
	aiSlot.setState(SLOT_EASY_AI);
	TheSkirmishGameInfo->setSlot(1, aiSlot);

	TheSkirmishGameInfo->setSeed((UnsignedInt)GetTickCount());
	TheSkirmishGameInfo->setStartingCash(prefs.getStartingCash());

	AsciiString mapName = explicitMap.isEmpty() ? prefs.getPreferredMap() : explicitMap;
	if (mapName.isEmpty())
	{
		// Fallback: pick the first multiplayer map in the cache.
		for (auto& kv : *TheMapCache)
		{
			if (kv.second.m_isMultiplayer)
			{
				mapName = kv.second.m_fileName;
				break;
			}
		}
	}
	if (mapName.isEmpty())
		return FALSE;   // no skirmish maps cached yet — try again next frame.

	TheSkirmishGameInfo->setMap(mapName);

	const MapMetaData *md = TheMapCache->findMap(mapName);
	if (md == nullptr)
	{
		AsciiString lowerMap = mapName;
		lowerMap.toLower();
		auto it = TheMapCache->find(lowerMap);
		if (it != TheMapCache->end())
			md = &it->second;
	}
	if (md != nullptr)
	{
		TheSkirmishGameInfo->setMapCRC(md->m_CRC);
		TheSkirmishGameInfo->setMapSize(md->m_filesize);
	}
	else
	{
		TheSkirmishGameInfo->setMapCRC(0);
		TheSkirmishGameInfo->setMapSize(0);
	}

	TheWritableGlobalData->m_mapName = TheSkirmishGameInfo->getMap();
	TheSkirmishGameInfo->startGame(0);

	InitRandom(TheSkirmishGameInfo->getSeed());

	GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_NEW_GAME);
	msg->appendIntegerArgument(GAME_SKIRMISH);
	msg->appendIntegerArgument(DIFFICULTY_NORMAL);
	msg->appendIntegerArgument(0);
	msg->appendIntegerArgument(30);   // FPS limit

	return TRUE;
}
