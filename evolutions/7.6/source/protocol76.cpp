//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
// Implementation of tibia v7.9x protocol
//////////////////////////////////////////////////////////////////////
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//////////////////////////////////////////////////////////////////////
#include "otpch.h"

#include "definitions.h"

#include <string>
#include <iostream>
#include <sstream>
#include <time.h>
#include <list>

#include "networkmessage.h"
#include "protocol76.h"

#include "items.h"

#include "tile.h"
#include "creature.h"
#include "player.h"
#include "status.h"
#include "chat.h"

#include <stdio.h>

#include "configmanager.h"

#include "otsystem.h"
#include "actions.h"
#include "game.h"
#include "ioplayer.h"
#include "house.h"
#include "waitlist.h"
#include "ban.h"

extern Game g_game;
extern ConfigManager g_config;
extern Actions actions;
extern Ban g_bans;
Chat g_chat;

Protocol76::Protocol76(SOCKET s)
{
	OTSYS_THREAD_LOCKVARINIT(bufferLock);
	
	player = NULL;
	pendingLogout = false;

	windowTextID = 0;
	readItem = NULL;
	maxTextLength = 0;
	this->s = s;
}

Protocol76::~Protocol76()
{
	OTSYS_THREAD_LOCKVARRELEASE(bufferLock);
	if(s){
		closesocket(s);
		s = 0;
	}
	player = NULL;
}

uint32_t Protocol76::getIP() const
{
	sockaddr_in sain;
	socklen_t salen = sizeof(sockaddr_in);
	if (getpeername(s, (sockaddr*)&sain, &salen) == 0)
	{
#if defined WIN32 || defined __WINDOWS__
		return sain.sin_addr.S_un.S_addr;
#else
		return sain.sin_addr.s_addr;
#endif
	}
	
	return 0;
}

void Protocol76::setPlayer(Player* p)
{
	player = p;
}

void Protocol76::sleepTillMove()
{
	long long delay = player->getSleepTicks();
	if(delay > 0 ){
		OTSYS_SLEEP((uint32_t)delay);
	}
}

void Protocol76::reinitializeProtocol(SOCKET _s)
{
	windowTextID = 0;
	readItem = NULL;
	maxTextLength = 0;
	OutputBuffer.Reset();
	knownCreatureList.clear();
	if(s)
		closesocket(s);
	s = _s;
}

connectResult_t Protocol76::ConnectPlayer()
{
	Waitlist* wait = Waitlist::instance();

	#ifdef __XID_ACCOUNT_MANAGER__
	if(player->getAccessLevel() < ACCESS_ENTER && !wait->clientLogin(player->getAccount(), player->getIP()) && player->getName() != "Account Manager"){
	#else
	if(player->getAccessLevel() < ACCESS_ENTER && !wait->clientLogin(player->getAccount(), player->getIP())){
	#endif
    	return CONNECT_TOMANYPLAYERS;
	}
	else{
		#ifdef __XID_ACCOUNT_MANAGER__
		if(player->getLastLoginSaved() == 0){
			if(g_game.placeCreature(player->getTemplePosition(), player, true)){
				return CONNECT_SUCCESS;
			}
		}
		else
		#endif
		{
			//last login position
			if(g_game.placeCreature(player->getLoginPosition(), player)){
				return CONNECT_SUCCESS;
			}
			//temple
			else if(g_game.placeCreature(player->getTemplePosition(), player, true)){
				return CONNECT_SUCCESS;
			}
			else{
				return CONNECT_MASTERPOSERROR;
			}
		}
	}

	return CONNECT_INTERNALERROR;
}


void Protocol76::ReceiveLoop()
{
	NetworkMessage msg;

	do{
		while(pendingLogout == false && msg.ReadFromSocket(s)){
			parsePacket(msg);
		}

		if(s){
			closesocket(s);
			s = 0;
		}
		// logout by disconnect?  -> kick
		if(pendingLogout == false){
			g_game.playerSetAttackedCreature(player, 0);

			while(player->hasCondition(CONDITION_INFIGHT) && !player->isRemoved() && s == 0){
				OTSYS_SLEEP(250);
			}

			OTSYS_THREAD_LOCK(g_game.gameLock, "Protocol76::ReceiveLoop()");

			if(!player->isRemoved()){
				if(s == 0){
					g_game.removeCreature(player);
				}
				/*else{ //tryller fix
					//set new key after reattaching
				}*/
			}

			OTSYS_THREAD_UNLOCK(g_game.gameLock, "Protocol76::ReceiveLoop()");
		}
	}while(s != 0 && !player->isRemoved());
}


void Protocol76::parsePacket(NetworkMessage &msg)
{
	if(msg.getMessageLength() <= 0)
		return;

	uint8_t recvbyte = msg.GetByte();
	//a dead player can not performs actions
	if((player->isRemoved() || player->getHealth() <= 0) && recvbyte != 0x14){
		OTSYS_SLEEP(10);
		return;
	}

	#ifdef __XID_ACCOUNT_MANAGER__
	if(player->getName() == "Account Manager"){
		switch(recvbyte){
			case 0x14:
				parseLogout(msg);
				break;
			case 0x96:
				parseSay(msg);
				break;

			default:
				sendCancelWalk();
				break;
		}
		
		g_game.flushSendBuffers();
		return;
	}
	#endif

	switch(recvbyte){
	case 0x14: // logout
		parseLogout(msg);
		break;

	case 0x1E: // keep alive / ping response
		parseRecievePing(msg);
		break;

	case 0x64: // move with steps
		parseAutoWalk(msg);
		break;

	case 0x65: // move north
		parseMoveNorth(msg);
		break;

	case 0x66: // move east
		parseMoveEast(msg);
		break;

	case 0x67: // move south
		parseMoveSouth(msg);
		break;

	case 0x68: // move west
		parseMoveWest(msg);
		break;

	case 0x69: // stop-autowalk
		parseStopAutoWalk(msg);
		break;

	case 0x6A:
		parseMoveNorthEast(msg);
		break;

	case 0x6B:
		parseMoveSouthEast(msg);
		break;

	case 0x6C:
		parseMoveSouthWest(msg);
		break;

	case 0x6D:
		parseMoveNorthWest(msg);
		break;

	case 0x6F: // turn north
		parseTurnNorth(msg);
		break;

	case 0x70: // turn east
		parseTurnEast(msg);
		break;

	case 0x71: // turn south
		parseTurnSouth(msg);
		break;

	case 0x72: // turn west
		parseTurnWest(msg);
		break;

	case 0x78: // throw item
		parseThrow(msg);
		break;

	case 0x7D: // Request trade
		parseRequestTrade(msg);
		break;

	case 0x7E: // Look at an item in trade
		parseLookInTrade(msg);
		break;

	case 0x7F: // Accept trade
		parseAcceptTrade(msg);
		break;

	case 0x80: // Close/cancel trade
		parseCloseTrade();
		break;

	case 0x82: // use item
		parseUseItem(msg);
		break;

	case 0x83: // use item
		parseUseItemEx(msg);
		break;

	case 0x84: // battle window
		parseBattleWindow(msg);
		break;

	case 0x85:	//rotate item
		parseRotateItem(msg);
		break;

	case 0x87: // close container
		parseCloseContainer(msg);
		break;

	case 0x88: //"up-arrow" - container
		parseUpArrowContainer(msg);
		break;

	case 0x89:
		parseTextWindow(msg);
		break;

	case 0x8A:
		parseHouseWindow(msg);
		break;

	case 0x8C: // throw item
		parseLookAt(msg);
		break;

	case 0x96:  // say something
		parseSay(msg);
		break;

	case 0x97: // request Channels
		parseGetChannels(msg);
		break;

	case 0x98: // open Channel
		parseOpenChannel(msg);
		break;

	case 0x99: // close Channel
		parseCloseChannel(msg);
		break;

	case 0x9A: // open priv
		parseOpenPriv(msg);
		break;

	case 0xA0: // set attack and follow mode
		parseFightModes(msg);
		break;

	case 0xA1: // attack
		parseAttack(msg);
		break;

	case 0xA2: //follow
		parseFollow(msg);
		break;

	#ifdef __PARTYSYSTEM__
	case 0xA3: // invite party
		parseInviteParty(msg);
		break;

	case 0xA4: // join party
		parseJoinParty(msg);
		break;

	case 0xA5: // revoke party
		parseRevokeParty(msg);
		break;

	case 0xA6: // pass leadership
		parsePassLeadership(msg);
		break;

	case 0xA7: // leave party
		parseLeaveParty();
		break;
    #endif

	case 0xAA:
		parseCreatePrivateChannel(msg);
		break;
		
	case 0xAB:
		parseChannelInvite(msg);
		break;
        
	case 0xAC:
		parseChannelExclude(msg);
		break;

	case 0xBE: // cancel move
		parseCancelMove(msg);
		break;

	case 0xC9: //client sends its position, unknown usage
		msg.GetPosition();
		break;

	case 0xCA: //client request to resend the container (happens when you store more than container maxsize)
		parseUpdateContainer(msg);
		break;

	case 0xD2: // request Outfit
		parseRequestOutfit(msg);
		break;

	case 0xD3: // set outfit
		parseSetOutfit(msg);
		break;

	case 0xDC:
		parseAddVip(msg);
		break;

	case 0xDD:
		parseRemVip(msg);
		break;

	#ifdef __XID_CTRL_Z__
	case 0xE6:
		parseBugReport(msg);
		break;
	#endif
	
	#ifdef __XID_CTRL_Y__
	case 0xE7:
		parseBanPlayer(msg);
		break;
	#endif

	default:
		break;
	}

	g_game.flushSendBuffers();
}

void Protocol76::GetTileDescription(const Tile* tile, NetworkMessage &msg)
{
	if(tile){
		int count = 0;
		if(tile->ground){
			msg.AddItem(tile->ground);
			count++;
		}

		ItemVector::const_iterator it;
		for(it = tile->topItems.begin(); ((it != tile->topItems.end()) && (count < 10)); ++it){
			msg.AddItem(*it);
			count++;
		}

		CreatureVector::const_iterator itc;
		for(itc = tile->creatures.begin(); ((itc != tile->creatures.end()) && (count < 10)); ++itc){
			bool known;
			unsigned long removedKnown;
			#ifdef __TC_GM_INVISIBLE__
			if(!(*itc)->gmInvisible || (*itc)->gmInvisible && (*itc)->getPlayer() == player)            
			#endif
            {
				checkCreatureAsKnown((*itc)->getID(), known, removedKnown);
				AddCreature(msg,*itc, known, removedKnown);
				count++;
			}
		}	

		for(it = tile->downItems.begin(); ((it != tile->downItems.end()) && (count < 10)); ++it){
			msg.AddItem(*it);
			count++;
		}
	}
}

void Protocol76::GetMapDescription(unsigned short x, unsigned short y, unsigned char z,
	unsigned short width, unsigned short height, NetworkMessage &msg)
{
	int skip = -1;
	int startz, endz, zstep = 0;

	if (z > 7) {
		startz = z - 2;
		endz = std::min(MAP_MAX_LAYERS - 1, z + 2);
		zstep = 1;
	}
	else {
		startz = 7;
		endz = 0;

		zstep = -1;
	}

	for(int nz = startz; nz != endz + zstep; nz += zstep){
		GetFloorDescription(msg, x, y, nz, width, height, z - nz, skip);
	}

	if(skip >= 0){
		msg.AddByte(skip);
		msg.AddByte(0xFF);
		//cc += skip;
	}
}

void Protocol76::GetFloorDescription(NetworkMessage& msg, int x, int y, int z, int width, int height, int offset, int& skip)
{
	Tile* tile;

	for(int nx = 0; nx < width; nx++){
		for(int ny = 0; ny < height; ny++){
			tile = g_game.getTile(x + nx + offset, y + ny + offset, z);
			if(tile){
				if(skip >= 0){
					msg.AddByte(skip);
					msg.AddByte(0xFF);
				}
				skip = 0;

				GetTileDescription(tile, msg);
			}
			else {
				skip++;
				if(skip == 0xFF){
					msg.AddByte(0xFF);
					msg.AddByte(0xFF);
					skip = -1;
				}
			}
		}
	}
}

void Protocol76::checkCreatureAsKnown(unsigned long id, bool &known, unsigned long &removedKnown)
{
	// loop through the known player and check if the given player is in
	std::list<uint32_t>::iterator i;
	for(i = knownCreatureList.begin(); i != knownCreatureList.end(); ++i)
	{
		if((*i) == id)
		{
			// know... make the creature even more known...
			knownCreatureList.erase(i);
			knownCreatureList.push_back(id);

			known = true;
			return;
		}
	}

	// ok, he is unknown...
	known = false;

	// ... but not in future
	knownCreatureList.push_back(id);

	// to many known players?
	if(knownCreatureList.size() > 150) //150 for 7.8x
	{
		// lets try to remove one from the end of the list
		for (int n = 0; n < 150; n++)
		{
			removedKnown = knownCreatureList.front();

			Creature *c = g_game.getCreatureByID(removedKnown);
			if ((!c) || (!canSee(c)))
				break;

			// this creature we can't remove, still in sight, so back to the end
			knownCreatureList.pop_front();
			knownCreatureList.push_back(removedKnown);
		}

		// hopefully we found someone to remove :S, we got only 150 tries
		// if not... lets kick some players with debug errors :)
		knownCreatureList.pop_front();
	}
	else
	{
		// we can cache without problems :)
		removedKnown = 0;
	}
}

bool Protocol76::canSee(const Creature* c) const
{
	if(c->isRemoved())
		return false;

	return canSee(c->getPosition());
}

bool Protocol76::canSee(const Position& pos) const
{
	return canSee(pos.x, pos.y, pos.z);
}

bool Protocol76::canSee(int x, int y, int z) const
{
	const Position& myPos = player->getPosition();

	if(myPos.z <= 7){
		//we are on ground level or above (7 -> 0)
		//view is from 7 -> 0
		if(z > 7){
			return false;
		}
	}
	else if(myPos.z >= 8){
		//we are underground (8 -> 15)
		//view is +/- 2 from the floor we stand on
		if(std::abs(myPos.z - z) > 2){
			return false;
		}
	}

	//negative offset means that the action taken place is on a lower floor than ourself
	int offsetz = myPos.z - z;

	if ((x >= myPos.x - 8 + offsetz) && (x <= myPos.x + 9 + offsetz) &&
		(y >= myPos.y - 6 + offsetz) && (y <= myPos.y + 7 + offsetz))
		return true;

	return false;
}

void Protocol76::logout()
{
	// we ask the game to remove us
	if(!player->isRemoved()){
		if(g_game.removeCreature(player)){
            if(s){
				closesocket(s);
				s = 0;
			}
			
			pendingLogout = true;
		}
	}
	else{
		pendingLogout = true;
	}
}

// Parse methods
void Protocol76::parseLogout(NetworkMessage& msg)
{
	if(player->hasCondition(CONDITION_INFIGHT) && !player->isRemoved()){
		player->sendCancelMessage(RET_YOUMAYNOTLOGOUTDURINGAFIGHT);
		return;
	}
	else{
		logout();
	}
}

void Protocol76::parseCreatePrivateChannel(NetworkMessage& msg)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(g_game.gameLock, "Protocol76::parseCreatePrivateChannel()");
	if(player->isRemoved()){
		return;
	}

	ChatChannel* channel = g_chat.createChannel(player, 0xFFFF);
	
	if(channel){
		if(channel->addUser(player)){	
			NetworkMessage msg;
			msg.AddByte(0xB2);
			msg.AddU16(channel->getId());
			msg.AddString(channel->getName());
			
			WriteBuffer(msg);
		}
	}
}

void Protocol76::parseChannelInvite(NetworkMessage& msg)
{
	std::string name = msg.GetString();
		
	OTSYS_THREAD_LOCK_CLASS lockClass(g_game.gameLock, "Protocol76::parseChannelInvite()");
	if(player->isRemoved()){
		return;
	}

	PrivateChatChannel* channel = g_chat.getPrivateChannel(player);

	if(channel){
		Player* invitePlayer = g_game.getPlayerByName(name);
		
		if(invitePlayer){
			channel->invitePlayer(player, invitePlayer);
		}
	}
}

void Protocol76::parseChannelExclude(NetworkMessage& msg)
{
	std::string name = msg.GetString();

	OTSYS_THREAD_LOCK_CLASS lockClass(g_game.gameLock, "Protocol76::parseChannelExclude()");
	if(player->isRemoved()){
		return;
	}

	PrivateChatChannel* channel = g_chat.getPrivateChannel(player);

	if(channel){
		Player* excludePlayer = g_game.getPlayerByName(name);
		
		if(excludePlayer){
			channel->excludePlayer(player, excludePlayer);
		}
	}
}

void Protocol76::parseGetChannels(NetworkMessage& msg)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(g_game.gameLock, "Protocol76::parseGetChannels()");
	if(player->isRemoved()){
		return;
	}

	sendChannelsDialog();
}

void Protocol76::parseOpenChannel(NetworkMessage& msg)
{
	unsigned short channelId = msg.GetU16();
	OTSYS_THREAD_LOCK_CLASS lockClass(g_game.gameLock, "Protocol76::parseOpenChannel()");
	if(player->isRemoved()){
		return;
	}

	if(g_chat.addUserToChannel(player, channelId)){
		sendChannel(channelId, g_chat.getChannelName(player, channelId));
	}
}

void Protocol76::parseCloseChannel(NetworkMessage &msg)
{
	unsigned short channelId = msg.GetU16();
	OTSYS_THREAD_LOCK_CLASS lockClass(g_game.gameLock, "Protocol76::parseCloseChannel()");
	if(player->isRemoved()){
		return;
	}

	g_chat.removeUserFromChannel(player, channelId);
}

void Protocol76::parseOpenPriv(NetworkMessage& msg)
{
	std::string receiver;
	receiver = msg.GetString();

	OTSYS_THREAD_LOCK_CLASS lockClass(g_game.gameLock, "Protocol76::parseOpenPriv()");
	if(player->isRemoved()){
		return;
	}

	Player* playerPriv = g_game.getPlayerByName(receiver);
	if(playerPriv){
		sendOpenPriv(playerPriv->getName());
	}
}

void Protocol76::parseCancelMove(NetworkMessage& msg)
{
	g_game.playerSetAttackedCreature(player, 0);
	g_game.playerFollowCreature(player, 0);
}

void Protocol76::parseDebug(NetworkMessage& msg)
{
	int dataLength = msg.getMessageLength() - 3;
	if(dataLength != 0){
		printf("data: ");
		int data = msg.GetByte();
		while(dataLength > 0){
			printf("%d ", data);
			if(--dataLength > 0)
				data = msg.GetByte();
		}
		printf("\n");
	}
}


void Protocol76::parseRecievePing(NetworkMessage& msg)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(g_game.gameLock, "Protocol76::parseReceivePing()");
	if(player->isRemoved()){
		return;
	}

	player->receivePing();
}

void Protocol76::parseAutoWalk(NetworkMessage& msg)
{
	// first we get all directions...
	std::list<Direction> path;
	size_t numdirs = msg.GetByte();
	for (size_t i = 0; i < numdirs; ++i) {
		uint8_t rawdir = msg.GetByte();
		Direction dir = SOUTH;

		switch(rawdir) {
		case 1: dir = EAST; break;
		case 2: dir = NORTHEAST; break;
		case 3: dir = NORTH; break;
		case 4: dir = NORTHWEST; break;
		case 5: dir = WEST; break;
		case 6: dir = SOUTHWEST; break;
		case 7: dir = SOUTH; break;
		case 8: dir = SOUTHEAST; break;

		default:
			continue;
		};

		path.push_back(dir);
	}

	g_game.playerAutoWalk(player, path);
}

void Protocol76::parseStopAutoWalk(NetworkMessage& msg)
{
	g_game.playerStopAutoWalk(player);
}

void Protocol76::parseMoveNorth(NetworkMessage& msg)
{
	sleepTillMove();
	#ifdef __TR_ANTI_AFK__
	player->notAfk();
	#endif

	g_game.movePlayer(player, NORTH);
}

void Protocol76::parseMoveEast(NetworkMessage& msg)
{
	sleepTillMove();
	#ifdef __TR_ANTI_AFK__
	player->notAfk();
	#endif

	g_game.movePlayer(player, EAST);
}

void Protocol76::parseMoveSouth(NetworkMessage& msg)
{
	sleepTillMove();
	#ifdef __TR_ANTI_AFK__
	player->notAfk();
	#endif

	g_game.movePlayer(player, SOUTH);
}

void Protocol76::parseMoveWest(NetworkMessage& msg)
{
	sleepTillMove();
	#ifdef __TR_ANTI_AFK__
	player->notAfk();
	#endif

	g_game.movePlayer(player, WEST);
}

void Protocol76::parseMoveNorthEast(NetworkMessage& msg)
{
	sleepTillMove();
	sleepTillMove();
	#ifdef __TR_ANTI_AFK__
	player->notAfk();
	#endif

	g_game.movePlayer(player, NORTHEAST);
}

void Protocol76::parseMoveSouthEast(NetworkMessage& msg)
{
	sleepTillMove();
	sleepTillMove();
	#ifdef __TR_ANTI_AFK__
	player->notAfk();
	#endif

	g_game.movePlayer(player, SOUTHEAST);
}

void Protocol76::parseMoveSouthWest(NetworkMessage& msg)
{
	sleepTillMove();
	sleepTillMove();
	#ifdef __TR_ANTI_AFK__
	player->notAfk();
	#endif

	g_game.movePlayer(player, SOUTHWEST);
}

void Protocol76::parseMoveNorthWest(NetworkMessage& msg)
{
	sleepTillMove();
	sleepTillMove();
	#ifdef __TR_ANTI_AFK__
	player->notAfk();
	#endif

	g_game.movePlayer(player, NORTHWEST);
}

void Protocol76::parseTurnNorth(NetworkMessage& msg)
{
	#ifdef __TR_ANTI_AFK__
	player->notAfk();
	#endif

	g_game.playerTurn(player, NORTH);
}

void Protocol76::parseTurnEast(NetworkMessage& msg)
{
	#ifdef __TR_ANTI_AFK__
	player->notAfk();
	#endif

	g_game.playerTurn(player, EAST);
}

void Protocol76::parseTurnSouth(NetworkMessage& msg)
{
	#ifdef __TR_ANTI_AFK__
	player->notAfk();
	#endif

	g_game.playerTurn(player, SOUTH);
}

void Protocol76::parseTurnWest(NetworkMessage& msg)
{
	#ifdef __TR_ANTI_AFK__
	player->notAfk();
	#endif
	g_game.playerTurn(player, WEST);
}

void Protocol76::parseRequestOutfit(NetworkMessage& msg)
{
	msg.Reset();
	
	msg.AddByte(0xC8);
	AddCreatureOutfit(msg, player, player->getDefaultOutfit());

	switch (player->getSex())
	{
		case PLAYERSEX_FEMALE:
			msg.AddByte(PLAYER_FEMALE_1);
			if (player->isPremium())
			   msg.AddByte(PLAYER_FEMALE_7);
			else
				msg.AddByte(PLAYER_FEMALE_4);
		break;
		case PLAYERSEX_MALE:
			msg.AddByte(PLAYER_MALE_1);
			if (player->isPremium())
			   msg.AddByte(PLAYER_MALE_7);
			else
				msg.AddByte(PLAYER_MALE_4);
		break;
		case PLAYERSEX_OLDMALE:
			msg.AddByte(160);
			msg.AddByte(160);
		break;
		default:
			msg.AddByte(PLAYER_MALE_1);
			msg.AddByte(PLAYER_MALE_7);
		}
   
	WriteBuffer(msg);
}

void Protocol76::parseSetOutfit(NetworkMessage& msg)
{
	uint8_t lookType = msg.GetByte();
	Outfit_t newOutfit;

	// only first 4 outfits
	uint8_t lastFemaleOutfit = 0x8B;
	uint8_t lastMaleOutfit = 0x83;

	// if premium then all 7 outfits
	if (player->getSex() == PLAYERSEX_FEMALE && player->isPremium())
		lastFemaleOutfit = 0x8E;
	else if (player->getSex() == PLAYERSEX_MALE && player->isPremium())
		lastMaleOutfit = 0x86;
	
	if ((player->getSex() == PLAYERSEX_FEMALE && lookType >= PLAYER_FEMALE_1 && lookType <= lastFemaleOutfit)
		|| (player->getSex() == PLAYERSEX_MALE && lookType >= PLAYER_MALE_1 && lookType <= lastMaleOutfit))
	{
		newOutfit.lookType = lookType;
		newOutfit.lookHead = msg.GetByte();
		newOutfit.lookBody = msg.GetByte();
		newOutfit.lookLegs = msg.GetByte();
		newOutfit.lookFeet = msg.GetByte();
	}

	g_game.playerChangeOutfit(player, newOutfit);
}

void Protocol76::parseUseItem(NetworkMessage& msg)
{
	Position pos = msg.GetPosition();
	uint16_t spriteId = msg.GetSpriteId();
	uint8_t stackpos = msg.GetByte();
	uint8_t index = msg.GetByte();
	bool isHotkey = (pos.x == 0xFFFF && pos.y == 0 && pos.z == 0);
	
	if(player->setFollowItem(pos)){
		player->setFollowItem(1, pos, stackpos, spriteId, Position(0xFFFF, 0, 0), 0, 0, index);
		return;
	}

	g_game.playerUseItem(player, pos, stackpos, index, spriteId, isHotkey);
}

void Protocol76::parseUseItemEx(NetworkMessage& msg)
{
	Position fromPos = msg.GetPosition();
	uint16_t fromSpriteId = msg.GetSpriteId();
	uint8_t fromStackPos = msg.GetByte();
	Position toPos = msg.GetPosition();
	uint16_t toSpriteId = msg.GetU16();
	uint8_t toStackPos = msg.GetByte();
	bool isHotkey = (fromPos.x == 0xFFFF && fromPos.y == 0 && fromPos.z == 0);

	if(player->setFollowItem(fromPos)){
		player->setFollowItem(2, fromPos, fromStackPos, fromSpriteId, toPos, toStackPos, toSpriteId, 0);
		return;
	}

	g_game.playerUseItemEx(player, fromPos, fromStackPos, fromSpriteId, toPos, toStackPos, toSpriteId, isHotkey);
}

void Protocol76::parseBattleWindow(NetworkMessage &msg)
{
	Position fromPos = msg.GetPosition();
	uint16_t spriteId = msg.GetSpriteId();
	uint8_t fromStackPos = msg.GetByte();
	uint32_t creatureId = msg.GetU32();
	bool isHotkey = (fromPos.x == 0xFFFF && fromPos.y == 0 && fromPos.z == 0);

	g_game.playerUseBattleWindow(player, fromPos, fromStackPos, creatureId, spriteId, isHotkey);
}

void Protocol76::parseCloseContainer(NetworkMessage& msg)
{
	unsigned char containerid = msg.GetByte();

	OTSYS_THREAD_LOCK_CLASS lockClass(g_game.gameLock, "Protocol76::parseCloseContainer()");
	if(player->isRemoved()){
		return;
	}

	player->closeContainer(containerid);
	sendCloseContainer(containerid);
}

void Protocol76::parseUpArrowContainer(NetworkMessage& msg)
{
	uint32_t cid = msg.GetByte();
	OTSYS_THREAD_LOCK_CLASS lockClass(g_game.gameLock, "Protocol76::parseUpArrowContainer()");
	if(player->isRemoved()){
		return;
	}

	Container* container = player->getContainer(cid);
	if(!container)
		return;

	Container* parentcontainer = dynamic_cast<Container*>(container->getParent());
	if(parentcontainer){
		bool hasParent = (dynamic_cast<const Container*>(parentcontainer->getParent()) != NULL);
		player->addContainer(cid, parentcontainer);
		sendContainer(cid, parentcontainer, hasParent);
	}
}

void Protocol76::parseUpdateContainer(NetworkMessage& msg)
{
	uint32_t cid = msg.GetByte();
	OTSYS_THREAD_LOCK_CLASS lockClass(g_game.gameLock, "Protocol76::parseUpdateContainer()");
	if(player->isRemoved()){
		return;
	}

	Container* container = player->getContainer(cid);
	if(!container)
		return;

	bool hasParent = (dynamic_cast<const Container*>(container->getParent()) != NULL);
	sendContainer(cid, container, hasParent);
}

void Protocol76::parseThrow(NetworkMessage& msg)
{
	Position fromPos = msg.GetPosition();
	uint16_t spriteId = msg.GetSpriteId();
	uint8_t fromStackpos = msg.GetByte();
	Position toPos = msg.GetPosition();
	uint8_t count = msg.GetByte();

	if(toPos == fromPos)
		return;

	if(player->setFollowItem(fromPos)){
		player->setFollowItem(3, fromPos, fromStackpos, spriteId, toPos, 0, 0, count);
		return;
	}

	g_game.thingMove(player, fromPos, spriteId, fromStackpos, toPos, count);
}

void Protocol76::parseLookAt(NetworkMessage& msg)
{
	Position pos = msg.GetPosition();
	uint16_t spriteId = msg.GetSpriteId();
	uint8_t stackpos = msg.GetByte();

	g_game.playerLookAt(player, pos, spriteId, stackpos);
}

void Protocol76::parseSay(NetworkMessage& msg)
{
	SpeakClasses type = (SpeakClasses)msg.GetByte();

	std::string receiver;
	unsigned short channelId = 0;
	if(type == SPEAK_PRIVATE ||
		type == SPEAK_PRIVATE_RED)
		receiver = msg.GetString();
	if(type == SPEAK_CHANNEL_Y ||
		type == SPEAK_CHANNEL_R1 ||
		type == SPEAK_CHANNEL_R2)
		channelId = msg.GetU16();
	std::string text = msg.GetString();

	#ifdef __XID_ACCOUNT_MANAGER__
	if(player->getName() == "Account Manager"){
		sendCreatureSay(player, type, text);
		g_game.manageAccount(player, text);
		return;
	}
	#endif

	g_game.playerSay(player, channelId, type, receiver, text);
}

void Protocol76::parseFightModes(NetworkMessage& msg)
{
	uint8_t rawFightMode = msg.GetByte(); //1 - offensive, 2 - balanced, 3 - defensive
	uint8_t rawChaseMode = msg.GetByte(); // 0 - stand while fightning, 1 - chase opponent
	uint8_t safeMode = msg.GetByte(); // 0 - can't attack, 1 - can attack

	chaseMode_t chaseMode = CHASEMODE_STANDSTILL;

	if(rawChaseMode == 0){
		chaseMode = CHASEMODE_STANDSTILL;
	}
	else if(rawChaseMode == 1){
		chaseMode = CHASEMODE_FOLLOW;
	}

	fightMode_t fightMode = FIGHTMODE_ATTACK;

	if(rawFightMode == 1){
		fightMode = FIGHTMODE_ATTACK;
	}
	else if(rawFightMode == 2){
		fightMode = FIGHTMODE_BALANCED;
	}
	else if(rawFightMode == 3){
		fightMode = FIGHTMODE_DEFENSE;
	}

	g_game.playerSetFightModes(player, fightMode, chaseMode, safeMode);
}

void Protocol76::parseAttack(NetworkMessage& msg)
{
	unsigned long creatureid = msg.GetU32();
	g_game.playerSetAttackedCreature(player, creatureid);
}

void Protocol76::parseFollow(NetworkMessage& msg)
{
	unsigned long creatureId = msg.GetU32();
	g_game.playerFollowCreature(player, creatureId);
}

void Protocol76::parseTextWindow(NetworkMessage& msg)
{
	unsigned long id = msg.GetU32();
	std::string new_text = msg.GetString();
	if(new_text.length() > maxTextLength)
		return;

	if(readItem && windowTextID == id){
		g_game.playerWriteItem(player, readItem, new_text);
		readItem->releaseThing2();
		readItem = NULL;
	}
}

void Protocol76::parseHouseWindow(NetworkMessage &msg)
{
	unsigned char _listid = msg.GetByte();
	unsigned long id = msg.GetU32();
	std::string new_list = msg.GetString();

	OTSYS_THREAD_LOCK_CLASS lockClass(g_game.gameLock, "Protocol76::parseHouseWindow()");
	if(player->isRemoved()){
		return;
	}

	if(house && windowTextID == id && _listid == 0){
		house->setAccessList(listId, new_list);
		house = NULL;
		listId = 0;
	}
}

void Protocol76::parseRequestTrade(NetworkMessage& msg)
{
	Position pos = msg.GetPosition();
	uint16_t spriteId = msg.GetSpriteId();
	uint8_t stackpos = msg.GetByte();
	uint32_t playerId = msg.GetU32();

	if(player->setFollowItem(pos)){
		player->setFollowItem(4, pos, stackpos, spriteId, Position(0xFFFF, 0, 0), 0, 0, playerId);
		return;
	}

	g_game.playerRequestTrade(player, pos, stackpos, playerId, spriteId);
}

void Protocol76::parseAcceptTrade(NetworkMessage& msg)
{
	g_game.playerAcceptTrade(player);
}

void Protocol76::parseLookInTrade(NetworkMessage& msg)
{
	bool counterOffer = (msg.GetByte() == 0x01);
	int index = msg.GetByte();

	g_game.playerLookInTrade(player, counterOffer, index);
}

void Protocol76::parseCloseTrade()
{
	g_game.playerCloseTrade(player);
}

void Protocol76::parseAddVip(NetworkMessage& msg)
{
	std::string name = msg.GetString();
	if(name.size() > 32)
		return;

	Player* vip_player = g_game.getPlayerByName(name.c_str());
	if(IOPlayer::instance()->playerExists(name)){
		bool success = false;
		for(int i = 0; i < MAX_VIPS; i++){
			if(player->vip[i].empty()){
				bool online = (g_game.getPlayerByName(name) != NULL);
				sendVIP(i+1, name, online);
				player->vip[i] = name;
				success = true;
				break;
			}
			else{
				std::string playerName = name;
				std::transform(playerName.begin(), playerName.end(), playerName.begin(), tolower);
				
				std::string vipName = player->vip[i];
				std::transform(vipName.begin(), vipName.end(), vipName.begin(), tolower);
				
				if(playerName == vipName){
					player->sendCancel("You have already added this player.");
					success = true;
					return;
				}
			}
		}
		if(!success)
			player->sendCancel("Your VIP list is currently full, please remove some of your VIP's before adding more.");
	}
	else
		player->sendCancel("A player with that name does not exist.");
}

void Protocol76::parseRemVip(NetworkMessage& msg)
{
	unsigned long id = msg.GetU32();
	if(id >= 1 && id <= MAX_VIPS)
		player->vip[id-1].clear();
}

void Protocol76::parseRotateItem(NetworkMessage& msg)
{
	Position pos = msg.GetPosition();
	uint16_t spriteId = msg.GetSpriteId();
	uint8_t stackpos = msg.GetByte();

	if(player->setFollowItem(pos)){
		player->setFollowItem(5, pos, stackpos, spriteId, Position(0xFFFF, 0, 0), 0, 0, 0);
		return;
	}

	g_game.playerRotateItem(player, pos, stackpos, spriteId);
}

// Send methods
void Protocol76::sendOpenPriv(const std::string& receiver)
{
	NetworkMessage newmsg;
	newmsg.AddByte(0xAD);
	newmsg.AddString(receiver);
	WriteBuffer(newmsg);
}

void Protocol76::sendCreatureOutfit(const Creature* creature, const Outfit_t& outfit)
{
	if(canSee(creature)){
		NetworkMessage msg;
		msg.AddByte(0x8E);
		msg.AddU32(creature->getID());
		AddCreatureOutfit(msg, creature, outfit);
		WriteBuffer(msg);
	}
}

void Protocol76::sendCreatureInvisible(const Creature* creature)
{
	if(canSee(creature)){
		NetworkMessage msg;
		msg.AddByte(0x8E);
		msg.AddU32(creature->getID());
		AddCreatureInvisible(msg, creature);
		WriteBuffer(msg);
	}
}

void Protocol76::sendCreatureLight(const Creature* creature)
{
	if(canSee(creature)){
		NetworkMessage msg;
		AddCreatureLight(msg, creature);
		WriteBuffer(msg);
	}
}

void Protocol76::sendWorldLight(const LightInfo& lightInfo)
{
	NetworkMessage msg;
	AddWorldLight(msg, lightInfo);
	WriteBuffer(msg);
}

void Protocol76::sendCreatureSkull(const Creature* creature, Skulls_t newSkull /*= SKULL_NONE*/)
{
	if(canSee(creature)){
		NetworkMessage msg;
		msg.AddByte(0x90);
		msg.AddU32(creature->getID());
		if(newSkull == SKULL_NONE){
			#ifdef __SKULLSYSTEM__
			if(const Player* playerSkull = creature->getPlayer()){
				msg.AddByte(player->getSkullClient(playerSkull));
			}
			else{
				msg.AddByte(0);	//no skull
			}
			#else
			msg.AddByte(0);	//no skull
			#endif
		}
		else{
			msg.AddByte(newSkull);
		}
		
		WriteBuffer(msg);
	}
}

void Protocol76::sendCreatureShield(const Creature* creature)
{
	if(canSee(creature)){
		NetworkMessage msg;
		msg.AddByte(0x91);
		msg.AddU32(creature->getID());
		#ifdef __PARTYSYSTEM__
		if(const Player* shieldPlayer = creature->getPlayer())
			msg.AddByte(player->getShieldClient(const_cast<Player*>(shieldPlayer)));
		else
			msg.AddByte(0);
		#else
		msg.AddByte(0);
		#endif
		WriteBuffer(msg);
	}
}

void Protocol76::sendCreatureSquare(const Creature* creature, SquareColor_t color)
{
	if(canSee(creature)){
		NetworkMessage msg;
		msg.AddByte(0x86);
		msg.AddU32(creature->getID());
		msg.AddByte((unsigned char)color);
		WriteBuffer(msg);
	}
}

void Protocol76::sendStats()
{
	NetworkMessage msg;
	AddPlayerStats(msg);
	WriteBuffer(msg);
}

void Protocol76::sendTextMessage(MessageClasses mclass, const std::string& message)
{
	NetworkMessage msg;
	AddTextMessage(msg,mclass, message);
	WriteBuffer(msg);
}

void Protocol76::sendClosePrivate(uint16_t channelId)
{
	NetworkMessage msg;

	msg.AddByte(0xB3);
	msg.AddU16(channelId);
		
	WriteBuffer(msg);
}

void Protocol76::sendChannelsDialog()
{
	NetworkMessage newmsg;
	ChannelList list;

	list = g_chat.getChannelList(player);

	newmsg.AddByte(0xAB);

	newmsg.AddByte(list.size()); //how many

	while(list.size()){
		ChatChannel *channel;
		channel = list.front();
		list.pop_front();

		newmsg.AddU16(channel->getId());
		newmsg.AddString(channel->getName());
	}

	WriteBuffer(newmsg);
}

void Protocol76::sendChannel(uint16_t channelId, const std::string& channelName)
{
	NetworkMessage newmsg;

	newmsg.AddByte(0xAC);
	newmsg.AddU16(channelId);
	newmsg.AddString(channelName);

	WriteBuffer(newmsg);
}

void Protocol76::sendIcons(int icons)
{
	NetworkMessage newmsg;
	newmsg.AddByte(0xA2);
	newmsg.AddByte(icons);
	WriteBuffer(newmsg);
}

void Protocol76::sendContainer(uint32_t cid, const Container* container, bool hasParent)
{
	NetworkMessage msg;

	msg.AddByte(0x6E);
	msg.AddByte(cid);

	msg.AddItemId(container);
	msg.AddString(container->getName());
	msg.AddByte(container->capacity());
	msg.AddByte(hasParent ? 0x01 : 0x00);
	if(container->size() > 255){
		msg.AddByte(255);
	}
	else{
		msg.AddByte(container->size());
	}

	ItemList::const_iterator cit;
	for(cit = container->getItems(); cit != container->getEnd(); ++cit){
		msg.AddItem(*cit);
	}

	WriteBuffer(msg);
}

void Protocol76::sendTradeItemRequest(const Player* player, const Item* item, bool ack)
{
	NetworkMessage msg;
	if(ack){
		msg.AddByte(0x7D);
	}
	else{
		msg.AddByte(0x7E);
	}

	msg.AddString(player->getName());

	if(const Container* tradeContainer = item->getContainer()){

		std::list<const Container*> listContainer;
		ItemList::const_iterator it;
		Container* tmpContainer = NULL;

		listContainer.push_back(tradeContainer);

		std::list<const Item*> listItem;
		listItem.push_back(tradeContainer);

		while(listContainer.size() > 0) {
			const Container* container = listContainer.front();
			listContainer.pop_front();

			for(it = container->getItems(); it != container->getEnd(); ++it){
				if(tmpContainer = (*it)->getContainer()){
					listContainer.push_back(tmpContainer);
				}

				listItem.push_back(*it);
			}
		}

		msg.AddByte(listItem.size());
		while(listItem.size() > 0) {
			const Item* item = listItem.front();
			listItem.pop_front();
			msg.AddItem(item);
		}
	}
	else {
		msg.AddByte(1);
		msg.AddItem(item);
	}

	WriteBuffer(msg);
}

void Protocol76::sendCloseTrade()
{
	NetworkMessage msg;
	msg.AddByte(0x7F);

	WriteBuffer(msg);
}

void Protocol76::sendCloseContainer(uint32_t cid)
{
	NetworkMessage msg;

	msg.AddByte(0x6F);
	msg.AddByte(cid);
	WriteBuffer(msg);
}

void Protocol76::sendCreatureTurn(const Creature* creature, unsigned char stackPos)
{
	if(canSee(creature)){
		NetworkMessage msg;

		msg.AddByte(0x6B);
		msg.AddPosition(creature->getPosition());
		msg.AddByte(stackPos);

		msg.AddU16(0x63); /*99*/
		msg.AddU32(creature->getID());
		msg.AddByte(creature->getDirection());
		WriteBuffer(msg);
	}
}

void Protocol76::sendCreatureSay(const Creature* creature, SpeakClasses type, const std::string& text)
{
	NetworkMessage msg;
	#ifdef __XID_ACCOUNT_MANAGER__
	if(creature->getName() != "Account Manager"){
		AddCreatureSpeak(msg,creature, type, text, 0);
	}
	else{
		player->sendTextMessage(MSG_STATUS_CONSOLE_ORANGE, ("You: " + text).c_str());
	}
    #else
	AddCreatureSpeak(msg,creature, type, text, 0);
	#endif
	WriteBuffer(msg);
}

void Protocol76::sendToChannel(const Creature * creature, SpeakClasses type, const std::string& text, unsigned short channelId){
	NetworkMessage msg;
	AddCreatureSpeak(msg,creature, type, text, channelId);
	WriteBuffer(msg);
}

void Protocol76::sendCancel(const std::string& message)
{
	NetworkMessage netmsg;
	AddTextMessage(netmsg, MSG_STATUS_SMALL, message);
	WriteBuffer(netmsg);
}

void Protocol76::sendCancelTarget()
{
	NetworkMessage netmsg;
	netmsg.AddByte(0xa3);
	WriteBuffer(netmsg);
}

void Protocol76::sendChangeSpeed(const Creature* creature, uint32_t speed)
{
	NetworkMessage netmsg;
	netmsg.AddByte(0x8F);

	netmsg.AddU32(creature->getID());
	netmsg.AddU16(speed);
	WriteBuffer(netmsg);
}

void Protocol76::sendCancelWalk()
{
	NetworkMessage netmsg;
	netmsg.AddByte(0xB5);
	netmsg.AddByte(player->getDirection());
	WriteBuffer(netmsg);
}

void Protocol76::sendSkills()
{
	NetworkMessage msg;
	AddPlayerSkills(msg);
	WriteBuffer(msg);
}

void Protocol76::sendPing()
{
	NetworkMessage msg;
	msg.AddByte(0x1E);
	WriteBuffer(msg);
}

void Protocol76::sendDistanceShoot(const Position& from, const Position& to, unsigned char type)
{
	NetworkMessage msg;
	AddDistanceShoot(msg,from, to,type);
	WriteBuffer(msg);
}

void Protocol76::sendMagicEffect(const Position& pos, unsigned char type)
{
	NetworkMessage msg;
	AddMagicEffect(msg, pos, type);
	WriteBuffer(msg);
}

void Protocol76::sendAnimatedText(const Position& pos, unsigned char color, std::string text)
{
	NetworkMessage msg;
	AddAnimatedText(msg, pos, color, text);
	WriteBuffer(msg);
}

void Protocol76::sendCreatureHealth(const Creature* creature)
{
	NetworkMessage msg;
	AddCreatureHealth(msg,creature);
	WriteBuffer(msg);
}

//tile
void Protocol76::sendAddTileItem(const Position& pos, const Item* item)
{
	if(canSee(pos)){
		NetworkMessage msg;
		AddTileItem(msg, pos, item);
		WriteBuffer(msg);
	}
}

void Protocol76::sendUpdateTileItem(const Position& pos, uint32_t stackpos, const Item* item)
{
	if(canSee(pos)){
		NetworkMessage msg;
		UpdateTileItem(msg, pos, stackpos, item);
		WriteBuffer(msg);
	}
}

void Protocol76::sendRemoveTileItem(const Position& pos, uint32_t stackpos)
{
	if(canSee(pos)){
		NetworkMessage msg;
		RemoveTileItem(msg, pos, stackpos);
		WriteBuffer(msg);
	}
}

void Protocol76::UpdateTile(const Position& pos)
{
	if(canSee(pos)){
		NetworkMessage msg;
		UpdateTile(msg, pos);
		WriteBuffer(msg);
	}
}

void Protocol76::sendAddCreature(const Creature* creature, bool isLogin)
{
	if(canSee(creature->getPosition())){
		NetworkMessage msg;

		if(creature == player){
			msg.AddByte(0x0A);
			msg.AddU32(player->getID());

			msg.AddByte(0x32);
			msg.AddByte(0x00);

			//can report bugs
			if(player->getAccessLevel() != 0){
				msg.AddByte(0x01);
			}
			else{
				msg.AddByte(0x00);
			}

			/* TODO GM ACTIONS?
			if(player->getAccessLevel() != 0){
				msg.AddByte(0x0B);
				msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);
				msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);
				msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);
				msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);
				msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);
				msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);
				msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);
				msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);msg.AddByte(0xFF);
			}
			*/
			if(player->getAccessLevel() > 1){
				msg.AddByte(0x0B);
				for (uint8_t i = 0; i < 32; i++) {
					msg.AddByte(0xFF);
				}
			}

			AddMapDescription(msg, player->getPosition());

			if(isLogin){
				AddMagicEffect(msg, player->getPosition(), NM_ME_ENERGY_AREA);
			}

			AddInventoryItem(msg, SLOT_HEAD, player->getInventoryItem(SLOT_HEAD));
			AddInventoryItem(msg, SLOT_NECKLACE, player->getInventoryItem(SLOT_NECKLACE));
			AddInventoryItem(msg, SLOT_BACKPACK, player->getInventoryItem(SLOT_BACKPACK));
			AddInventoryItem(msg, SLOT_ARMOR, player->getInventoryItem(SLOT_ARMOR));
			AddInventoryItem(msg, SLOT_RIGHT, player->getInventoryItem(SLOT_RIGHT));
			AddInventoryItem(msg, SLOT_LEFT, player->getInventoryItem(SLOT_LEFT));
			AddInventoryItem(msg, SLOT_LEGS, player->getInventoryItem(SLOT_LEGS));
			AddInventoryItem(msg, SLOT_FEET, player->getInventoryItem(SLOT_FEET));
			AddInventoryItem(msg, SLOT_RING, player->getInventoryItem(SLOT_RING));
			AddInventoryItem(msg, SLOT_AMMO, player->getInventoryItem(SLOT_AMMO));

			AddPlayerStats(msg);
			AddPlayerSkills(msg);

			//gameworld light-settings
			LightInfo lightInfo;
			g_game.getWorldLightInfo(lightInfo);
			AddWorldLight(msg, lightInfo);

			//player light level
			AddCreatureLight(msg, creature);

			std::string tempstring = g_config.getString(ConfigManager::LOGIN_MSG);
			if(tempstring.size() > 0){
				AddTextMessage(msg, MSG_STATUS_DEFAULT, tempstring.c_str());
			}

			#ifdef __XID_ACCOUNT_MANAGER__
			if(player->getName() == "Account Manager"){
				if(player->isAccountManager){
                    AddTextMessage(msg, MSG_STATUS_CONSOLE_BLUE, "Account Manager: Hello there. Nice to see you again. How can i help you?");
                    AddTextMessage(msg, MSG_STATUS_CONSOLE_BLUE, "Account Manager: Do you want to create a new \'character\' or change your \'password\'?");
				}
				else{
                    AddTextMessage(msg, MSG_STATUS_CONSOLE_BLUE, "Account Manager: Hey, my name is Account Manager.");
                    AddTextMessage(msg, MSG_STATUS_CONSOLE_BLUE, "Account Manager: If you want, I can create a new \'account\' for you.");
                    player->yesorno[3] = true;
				}

			}
			else{
				if(player->getLastLoginSaved() == 0){
					tempstring = "It\'s your first visit, please choose your outfit.";
					NetworkMessage msg;
					parseRequestOutfit(msg);
				}
				else{
					tempstring = "Your last visit was on ";
					time_t lastlogin = player->getLastLoginSaved();
					tempstring += ctime(&lastlogin);
					tempstring.erase(tempstring.length() -1);
					tempstring += ".";
				}
			}
			#else
			if(player->getLastLoginSaved() == 0){
				tempstring = "It\'s your first visit, please choose your outfit.";
				NetworkMessage msg;
				parseRequestOutfit(msg);
			}
			else{
				tempstring = "Your last visit was on ";
				time_t lastlogin = player->getLastLoginSaved();
				tempstring += ctime(&lastlogin);
				tempstring.erase(tempstring.length() -1);
				tempstring += ".";
			}
            #endif
            
			WriteBuffer(msg);

			for(int i = 0; i < MAX_VIPS; i++){
				if(!player->vip[i].empty()){
					bool online = (g_game.getPlayerByName(player->vip[i]) != NULL);
					sendVIP(i+1, player->vip[i], online);
				}
			}

			//force flush
			flushOutputBuffer();
		}
		else{
			AddTileCreature(msg, creature->getPosition(), creature);

			if(isLogin){
				AddMagicEffect(msg, creature->getPosition(), NM_ME_ENERGY_AREA);
			}

			WriteBuffer(msg);
		}
	}
}

void Protocol76::sendRemoveCreature(const Creature* creature, const Position& pos, uint32_t stackpos, bool isLogout)
{
	if(canSee(pos)){
		NetworkMessage msg;
		RemoveTileItem(msg, pos, stackpos);

		if(isLogout){
			AddMagicEffect(msg, pos, NM_ME_PUFF);
		}

		WriteBuffer(msg);
	}
}

void Protocol76::sendMoveCreature(const Creature* creature, const Position& newPos, const Position& oldPos,
	uint32_t oldStackPos, bool teleport)
{
	if(creature == player){
		NetworkMessage msg;

		if(teleport){
			RemoveTileItem(msg, oldPos, oldStackPos);
			AddMapDescription(msg, player->getPosition());
		}
		else{
			if(oldPos.z == 7 && newPos.z >= 8){
				RemoveTileItem(msg, oldPos, oldStackPos);
			}
			else{
				if(oldStackPos < 10){
					msg.AddByte(0x6D);
					msg.AddPosition(oldPos);
					msg.AddByte(oldStackPos);
					msg.AddPosition(creature->getPosition());
				}
			}

			//floor change down
			if(newPos.z > oldPos.z){
				MoveDownCreature(msg, creature, newPos, oldPos, oldStackPos);
			}
			//floor change up
			else if(newPos.z < oldPos.z){
				MoveUpCreature(msg, creature, newPos, oldPos, oldStackPos);
			}

			if(oldPos.y > newPos.y){ // north, for old x
				msg.AddByte(0x65);
				GetMapDescription(oldPos.x - 8, newPos.y - 6, newPos.z, 18, 1, msg);
			}
			else if(oldPos.y < newPos.y){ // south, for old x
				msg.AddByte(0x67);
				GetMapDescription(oldPos.x - 8, newPos.y + 7, newPos.z, 18, 1, msg);
			}

			if(oldPos.x < newPos.x){ // east, [with new y]
				msg.AddByte(0x66);
				GetMapDescription(newPos.x + 9, newPos.y - 6, newPos.z, 1, 14, msg);
			}
			else if(oldPos.x > newPos.x){ // west, [with new y]
				msg.AddByte(0x68);
				GetMapDescription(newPos.x - 8, newPos.y - 6, newPos.z, 1, 14, msg);
			}
		}

		WriteBuffer(msg);
	}
	else if(canSee(oldPos) && canSee(creature->getPosition())){
		if(teleport || (oldPos.z == 7 && newPos.z >= 8)){
			sendRemoveCreature(creature, oldPos, oldStackPos, false);
			sendAddCreature(creature, false);
		}
		else{
			if(oldStackPos < 10){
				NetworkMessage msg;

				msg.AddByte(0x6D);
				msg.AddPosition(oldPos);
				msg.AddByte(oldStackPos);
				msg.AddPosition(creature->getPosition());
				WriteBuffer(msg);
			}
		}
	}
	else if(canSee(oldPos)){
		sendRemoveCreature(creature, oldPos, oldStackPos, false);
	}
	else if(canSee(creature->getPosition())){
		sendAddCreature(creature, false);
	}
}

//inventory
void Protocol76::sendAddInventoryItem(slots_t slot, const Item* item)
{
	NetworkMessage msg;
	AddInventoryItem(msg, slot, item);
	WriteBuffer(msg);
}

void Protocol76::sendUpdateInventoryItem(slots_t slot, const Item* item)
{
	NetworkMessage msg;
	UpdateInventoryItem(msg, slot, item);
	WriteBuffer(msg);
}

void Protocol76::sendRemoveInventoryItem(slots_t slot)
{
	NetworkMessage msg;
	RemoveInventoryItem(msg, slot);
	WriteBuffer(msg);
}

//containers
void Protocol76::sendAddContainerItem(uint8_t cid, const Item* item)
{
	NetworkMessage msg;
	AddContainerItem(msg, cid, item);
	WriteBuffer(msg);
}

void Protocol76::sendUpdateContainerItem(uint8_t cid, uint8_t slot, const Item* item)
{
	NetworkMessage msg;
	UpdateContainerItem(msg, cid, slot, item);
	WriteBuffer(msg);
}

void Protocol76::sendRemoveContainerItem(uint8_t cid, uint8_t slot)
{
	NetworkMessage msg;
	RemoveContainerItem(msg, cid, slot);
	WriteBuffer(msg);
}

void Protocol76::sendTextWindow(Item* item,const unsigned short maxlen, const bool canWrite)
{
	NetworkMessage msg;
	if(readItem){
		readItem->releaseThing2();
	}
	windowTextID++;
	msg.AddByte(0x96);
	msg.AddU32(windowTextID);
	msg.AddItemId(item);
	if(canWrite){
		msg.AddU16(maxlen);
		msg.AddString(item->getText());
		item->useThing2();
		readItem = item;
		maxTextLength = maxlen;
	}
	else{
		msg.AddU16(item->getText().size());
		msg.AddString(item->getText());
		readItem = NULL;
		maxTextLength = 0;
	}
	msg.AddString("unknown");
	msg.AddString("unknown");
	WriteBuffer(msg);
}

void Protocol76::sendTextWindow(uint32_t itemid, const std::string& text)
{
	NetworkMessage msg;
	if(readItem){
		readItem->releaseThing2();
	}
	windowTextID++;
	msg.AddByte(0x96);
	msg.AddU32(windowTextID);
	msg.AddItemId(itemid);
	
	msg.AddU16(text.size());
	msg.AddString(text);
	readItem = NULL;
	maxTextLength = 0;
	
	msg.AddString("");
	msg.AddString("");
	WriteBuffer(msg);
}

void Protocol76::sendHouseWindow(House* _house, unsigned long _listid, const std::string& text)
{
	NetworkMessage msg;
	windowTextID++;
	house = _house;
	listId = _listid;
	msg.AddByte(0x97);
	msg.AddByte(0);
	msg.AddU32(windowTextID);
	msg.AddString(text);
	WriteBuffer(msg);
}

void Protocol76::sendVIPLogIn(unsigned long guid)
{
	NetworkMessage msg;
	msg.AddByte(0xD3);
	msg.AddU32(guid);
	WriteBuffer(msg);
}

void Protocol76::sendVIPLogOut(unsigned long guid)
{
	NetworkMessage msg;
	msg.AddByte(0xD4);
	msg.AddU32(guid);
	WriteBuffer(msg);
}

void Protocol76::sendVIP(unsigned long guid, const std::string& name, bool isOnline)
{
	NetworkMessage msg;
	msg.AddByte(0xD2);
	msg.AddU32(guid);
	msg.AddString(name);
	msg.AddByte(isOnline == true ? 1 : 0);
	WriteBuffer(msg);
}

void Protocol76::sendReLoginWindow()
{
	NetworkMessage msg;
	msg.AddByte(0x28);
	WriteBuffer(msg);
}

////////////// Add common messages
void Protocol76::AddMapDescription(NetworkMessage& msg, const Position& pos)
{
	msg.AddByte(0x64);
	msg.AddPosition(player->getPosition());
	GetMapDescription(pos.x - 8, pos.y - 6, pos.z, 18, 14, msg);
}

void Protocol76::AddTextMessage(NetworkMessage& msg, MessageClasses mclass, const std::string& message)
{
	msg.AddByte(0xB4);
	msg.AddByte(mclass);
	msg.AddString(message);
}

void Protocol76::AddAnimatedText(NetworkMessage& msg, const Position& pos,
	unsigned char color, const std::string& text)
{
	msg.AddByte(0x84);
	msg.AddPosition(pos);
	msg.AddByte(color);
	msg.AddString(text);
}

void Protocol76::AddMagicEffect(NetworkMessage& msg,const Position& pos, unsigned char type)
{
	msg.AddByte(0x83);
	msg.AddPosition(pos);
	msg.AddByte(type + 1);
}


void Protocol76::AddDistanceShoot(NetworkMessage& msg, const Position& from, const Position& to,
	unsigned char type)
{
	msg.AddByte(0x85);
	msg.AddPosition(from);
	msg.AddPosition(to);
	msg.AddByte(type + 1);
}

void Protocol76::AddCreature(NetworkMessage &msg,const Creature* creature, bool known, unsigned int remove)
{
	if(known){
		msg.AddU16(0x62);
		msg.AddU32(creature->getID());
	}
	else{
		msg.AddU16(0x61);
		msg.AddU32(remove);
		msg.AddU32(creature->getID());
		msg.AddString(creature->getName());
	}

	#ifdef __TC_GM_INVISIBLE__
	if(creature->gmInvisible)
		msg.AddByte(0);
	else
		msg.AddByte((int32_t)std::ceil(((float)creature->getHealth()) * 100 / std::max(creature->getMaxHealth(), (int32_t)1)));
	#else
		msg.AddByte((int32_t)std::ceil(((float)creature->getHealth()) * 100 / std::max(creature->getMaxHealth(), (int32_t)1)));
	#endif
	
	msg.AddByte((uint8_t)creature->getDirection());

	if(!creature->isInvisible()){
		AddCreatureOutfit(msg, creature, creature->getCurrentOutfit());
	}
	else{
		AddCreatureInvisible(msg, creature);
	}

	LightInfo lightInfo;
	creature->getCreatureLight(lightInfo);
	msg.AddByte(lightInfo.level);
	msg.AddByte(lightInfo.color);

	msg.AddU16(creature->getSpeed());
	#ifdef __SKULLSYSTEM__
	msg.AddByte(player->getSkullClient(creature->getPlayer()));
	#else
	msg.AddByte(SKULL_NONE);
	#endif
	#ifdef __PARTYSYSTEM__
	if(const Player* shieldPlayer = creature->getPlayer()){
		msg.AddByte(player->getShieldClient(const_cast<Player*>(shieldPlayer)));
	}
	else{
		msg.AddByte(0);
	}
	#else
	msg.AddByte(0);
	#endif
}


void Protocol76::AddPlayerStats(NetworkMessage& msg)
{
	msg.AddByte(0xA0);
	msg.AddU16(player->getHealth());
	msg.AddU16(player->getPlayerInfo(PLAYERINFO_MAXHEALTH));
	msg.AddU16((unsigned short)std::floor(player->getFreeCapacity()));
	if(player->getPlayerInfo(PLAYERINFO_LEVEL) > 65535){
		msg.AddU32(player->getPlayerInfo(PLAYERINFO_LEVEL));
		msg.AddU16(0);
	}
	else if(player->getExperience() > 2000000000L){
		msg.AddU32(0);
		msg.AddU16(player->getPlayerInfo(PLAYERINFO_LEVEL));
	}
	else{
		msg.AddU32((unsigned long)player->getExperience());
		msg.AddU16(player->getPlayerInfo(PLAYERINFO_LEVEL));
	}
	msg.AddByte(player->getPlayerInfo(PLAYERINFO_LEVELPERCENT));
	msg.AddU16(player->getMana());
	msg.AddU16(player->getPlayerInfo(PLAYERINFO_MAXMANA));
	msg.AddByte(player->getMagicLevel());
	msg.AddByte(player->getPlayerInfo(PLAYERINFO_MAGICLEVELPERCENT));
	msg.AddByte(player->getPlayerInfo(PLAYERINFO_SOUL));
}

void Protocol76::AddPlayerSkills(NetworkMessage& msg)
{
	msg.AddByte(0xA1);

	msg.AddByte(player->getSkill(SKILL_FIST,   SKILL_LEVEL));
	msg.AddByte(player->getSkill(SKILL_FIST,   SKILL_PERCENT));
	msg.AddByte(player->getSkill(SKILL_CLUB,   SKILL_LEVEL));
	msg.AddByte(player->getSkill(SKILL_CLUB,   SKILL_PERCENT));
	msg.AddByte(player->getSkill(SKILL_SWORD,  SKILL_LEVEL));
	msg.AddByte(player->getSkill(SKILL_SWORD,  SKILL_PERCENT));
	msg.AddByte(player->getSkill(SKILL_AXE,    SKILL_LEVEL));
	msg.AddByte(player->getSkill(SKILL_AXE,    SKILL_PERCENT));
	msg.AddByte(player->getSkill(SKILL_DIST,   SKILL_LEVEL));
	msg.AddByte(player->getSkill(SKILL_DIST,   SKILL_PERCENT));
	msg.AddByte(player->getSkill(SKILL_SHIELD, SKILL_LEVEL));
	msg.AddByte(player->getSkill(SKILL_SHIELD, SKILL_PERCENT));
	msg.AddByte(player->getSkill(SKILL_FISH,   SKILL_LEVEL));
	msg.AddByte(player->getSkill(SKILL_FISH,   SKILL_PERCENT));
}

void Protocol76::AddCreatureSpeak(NetworkMessage& msg,const Creature* creature, SpeakClasses  type, std::string text, unsigned short channelId)
{
	msg.AddByte(0xAA);
	//msg.AddU32(0);
	msg.AddString(creature->getName());

	msg.AddByte(type);
	switch(type){
		case SPEAK_SAY:
		case SPEAK_WHISPER:
		case SPEAK_YELL:
		case SPEAK_MONSTER_SAY:
		case SPEAK_MONSTER_YELL:
			msg.AddPosition(creature->getPosition());
			break;
		case SPEAK_CHANNEL_Y:
		case SPEAK_CHANNEL_R1:
		case SPEAK_CHANNEL_R2:
		case SPEAK_CHANNEL_O:
			msg.AddU16(channelId);
			break;
		default:
			break;
	}
	msg.AddString(text);
}

void Protocol76::AddCreatureHealth(NetworkMessage& msg,const Creature* creature)
{
	msg.AddByte(0x8C);
	msg.AddU32(creature->getID());
	#ifdef __TC_GM_INVISIBLE__
	if(creature->gmInvisible)
		msg.AddByte(0);
	else
		msg.AddByte((int32_t)std::ceil(((float)creature->getHealth()) * 100 / std::max(creature->getMaxHealth(), (int32_t)1)));
	#else
		msg.AddByte((int32_t)std::ceil(((float)creature->getHealth()) * 100 / std::max(creature->getMaxHealth(), (int32_t)1)));
	#endif
}

void Protocol76::AddCreatureInvisible(NetworkMessage& msg, const Creature* creature)
{
	if(player->canSeeInvisibility()) {
		AddCreatureOutfit(msg, creature, creature->getCurrentOutfit());
	}
	else {
	    msg.AddByte(0);
	    msg.AddU16(0);
	}
}

void Protocol76::AddCreatureOutfit(NetworkMessage& msg, const Creature* creature, const Outfit_t& outfit)
{
	msg.AddByte(outfit.lookType);

	if(outfit.lookType != 0){
		msg.AddByte(outfit.lookHead);
		msg.AddByte(outfit.lookBody);
		msg.AddByte(outfit.lookLegs);
		msg.AddByte(outfit.lookFeet);
	}
	else{
		msg.AddItemId(outfit.lookTypeEx);
	}
}

void Protocol76::AddWorldLight(NetworkMessage& msg, const LightInfo& lightInfo)
{
	msg.AddByte(0x82);
	msg.AddByte(lightInfo.level);
	msg.AddByte(lightInfo.color);
}

void Protocol76::AddCreatureLight(NetworkMessage& msg, const Creature* creature)
{
	LightInfo lightInfo;
	creature->getCreatureLight(lightInfo);
	msg.AddByte(0x8D);
	msg.AddU32(creature->getID());
	msg.AddByte(lightInfo.level);
	msg.AddByte(lightInfo.color);
}

//tile
void Protocol76::AddTileItem(NetworkMessage& msg, const Position& pos, const Item* item)
{
	msg.AddByte(0x6A);
	msg.AddPosition(pos);
	msg.AddItem(item);
}

void Protocol76::AddTileCreature(NetworkMessage& msg, const Position& pos, const Creature* creature)
{
	msg.AddByte(0x6A);
	msg.AddPosition(pos);

	bool known;
	unsigned long removedKnown;
	checkCreatureAsKnown(creature->getID(), known, removedKnown);
	AddCreature(msg, creature, known, removedKnown);
}

void Protocol76::UpdateTileItem(NetworkMessage& msg, const Position& pos, uint32_t stackpos, const Item* item)
{
	if(stackpos < 10){
		msg.AddByte(0x6B);
		msg.AddPosition(pos);
		msg.AddByte(stackpos);
		msg.AddItem(item);
	}
}

void Protocol76::RemoveTileItem(NetworkMessage& msg, const Position& pos, uint32_t stackpos)
{
	if(stackpos < 10){
		msg.AddByte(0x6C);
		msg.AddPosition(pos);
		msg.AddByte(stackpos);
	}
}

void Protocol76::UpdateTile(NetworkMessage& msg, const Position& pos)
{
	msg.AddByte(0x69);
	msg.AddPosition(pos);

	Tile* tile = g_game.getTile(pos.x, pos.y, pos.z);
	if(tile){
		GetTileDescription(tile, msg);
		msg.AddByte(0);
		msg.AddByte(0xFF);
	}
	else{
		msg.AddByte(0x01);
		msg.AddByte(0xFF);
	}
}

void Protocol76::MoveUpCreature(NetworkMessage& msg, const Creature* creature,
	const Position& newPos, const Position& oldPos, uint32_t oldStackPos)
{
	if(creature == player){
		//floor change up
		msg.AddByte(0xBE);

		//going to surface
		if(newPos.z == 7){
			int skip = -1;
			GetFloorDescription(msg, oldPos.x - 8, oldPos.y - 6, 5, 18, 14, 3, skip); //(floor 7 and 6 already set)
			GetFloorDescription(msg, oldPos.x - 8, oldPos.y - 6, 4, 18, 14, 4, skip);
			GetFloorDescription(msg, oldPos.x - 8, oldPos.y - 6, 3, 18, 14, 5, skip);
			GetFloorDescription(msg, oldPos.x - 8, oldPos.y - 6, 2, 18, 14, 6, skip);
			GetFloorDescription(msg, oldPos.x - 8, oldPos.y - 6, 1, 18, 14, 7, skip);
			GetFloorDescription(msg, oldPos.x - 8, oldPos.y - 6, 0, 18, 14, 8, skip);

			if(skip >= 0){
				msg.AddByte(skip);
				msg.AddByte(0xFF);
			}
		}
		//underground, going one floor up (still underground)
		else if(newPos.z > 7){
			int skip = -1;
			GetFloorDescription(msg, oldPos.x - 8, oldPos.y - 6, oldPos.z - 3, 18, 14, 3, skip);

			if(skip >= 0){
				msg.AddByte(skip);
				msg.AddByte(0xFF);
			}
		}

		//moving up a floor up makes us out of sync
		//west
		msg.AddByte(0x68);
		GetMapDescription(oldPos.x - 8, oldPos.y + 1 - 6, newPos.z, 1, 14, msg);

		//north
		msg.AddByte(0x65);
		GetMapDescription(oldPos.x - 8, oldPos.y - 6, newPos.z, 18, 1, msg);
	}
}

void Protocol76::MoveDownCreature(NetworkMessage& msg, const Creature* creature,
	const Position& newPos, const Position& oldPos, uint32_t oldStackPos)
{
	if(creature == player){
		//floor change down
		msg.AddByte(0xBF);

		//going from surface to underground
		if(newPos.z == 8){
			int skip = -1;

			GetFloorDescription(msg, oldPos.x - 8, oldPos.y - 6, newPos.z, 18, 14, -1, skip);
			GetFloorDescription(msg, oldPos.x - 8, oldPos.y - 6, newPos.z + 1, 18, 14, -2, skip);
			GetFloorDescription(msg, oldPos.x - 8, oldPos.y - 6, newPos.z + 2, 18, 14, -3, skip);

			if(skip >= 0){
				msg.AddByte(skip);
				msg.AddByte(0xFF);
			}
		}
		//going further down
		else if(newPos.z > oldPos.z && newPos.z > 8 && newPos.z < 14){
			int skip = -1;
			GetFloorDescription(msg, oldPos.x - 8, oldPos.y - 6, newPos.z + 2, 18, 14, -3, skip);

			if(skip >= 0){
				msg.AddByte(skip);
				msg.AddByte(0xFF);
			}
		}

		//moving down a floor makes us out of sync
		//east
		msg.AddByte(0x66);
		GetMapDescription(oldPos.x + 9, oldPos.y - 1 - 6, newPos.z, 1, 14, msg);

		//south
		msg.AddByte(0x67);
		GetMapDescription(oldPos.x - 8, oldPos.y + 7, newPos.z, 18, 1, msg);
	}
}


//inventory
void Protocol76::AddInventoryItem(NetworkMessage& msg, slots_t slot, const Item* item)
{
	if(item == NULL){
		msg.AddByte(0x79);
		msg.AddByte(slot);
	}
	else{
		msg.AddByte(0x78);
		msg.AddByte(slot);
		msg.AddItem(item);
	}
}

void Protocol76::UpdateInventoryItem(NetworkMessage& msg, slots_t slot, const Item* item)
{
	if(item == NULL){
		msg.AddByte(0x79);
		msg.AddByte(slot);
	}
	else{
		msg.AddByte(0x78);
		msg.AddByte(slot);
		msg.AddItem(item);
	}
}

void Protocol76::RemoveInventoryItem(NetworkMessage& msg, slots_t slot)
{
	msg.AddByte(0x79);
	msg.AddByte(slot);
}

//containers
void Protocol76::AddContainerItem(NetworkMessage& msg, uint8_t cid, const Item* item)
{
	msg.AddByte(0x70);
	msg.AddByte(cid);
	msg.AddItem(item);
}

void Protocol76::UpdateContainerItem(NetworkMessage& msg, uint8_t cid, uint8_t slot, const Item* item)
{
	msg.AddByte(0x71);
	msg.AddByte(cid);
	msg.AddByte(slot);
	msg.AddItem(item);
}

void Protocol76::RemoveContainerItem(NetworkMessage& msg, uint8_t cid, uint8_t slot)
{
	msg.AddByte(0x72);
	msg.AddByte(cid);
	msg.AddByte(slot);
}

//////////////////////////

void Protocol76::flushOutputBuffer()
{
	OTSYS_THREAD_LOCK_CLASS lockClass(bufferLock, "Protocol76::flushOutputBuffer()");
	//force writetosocket
	OutputBuffer.WriteToSocket(s);
	OutputBuffer.Reset();

	return;
}

void Protocol76::WriteBuffer(NetworkMessage& addMsg)
{
	if(!addMsg.empty()){
		g_game.addPlayerBuffer(player);

		OTSYS_THREAD_LOCK(bufferLock, "Protocol76::WriteBuffer");

		if(OutputBuffer.getMessageLength() + addMsg.getMessageLength() >= NETWORKMESSAGE_MAXSIZE - 16){
			flushOutputBuffer();
		}

		OutputBuffer.JoinMessages(addMsg);
		OTSYS_THREAD_UNLOCK(bufferLock, "Protocol76::WriteBuffer");
	}
}

#ifdef __PARTYSYSTEM__
void Protocol76::parseInviteParty(NetworkMessage &msg)
{
    unsigned long creatureid = msg.GetU32();
    Creature* creature = g_game.getCreatureByID(creatureid);
    Player* target = dynamic_cast<Player*>(creature);

    if(!target)
        return;
    if(!player->party){
        Party::createParty(player, target);
        return;
    }
    if(player->party->getLeader() != player)
        return;

    player->party->invitePlayer(target);
}

void Protocol76::parseRevokeParty(NetworkMessage &msg)
{
    unsigned long cid = msg.GetU32();
    Player* target = g_game.getPlayerByID(cid);
    if(target && player->party)
        player->party->revokeInvitation(target);
}

void Protocol76::parseJoinParty(NetworkMessage &msg)
{
    unsigned long cid = msg.GetU32();
    Player* target = g_game.getPlayerByID(cid);
    
    if(target && target->party)
        target->party->acceptInvitation(player);
}

void Protocol76::parsePassLeadership(NetworkMessage &msg)
{
    unsigned long cid = msg.GetU32();
    Player* target = g_game.getPlayerByID(cid);
    if(target && player->party && player->party->getLeader() == player)
        player->party->passLeadership(target);
}

void Protocol76::parseLeaveParty()
{
    if(player->party)
        player->party->kickPlayer(player);
}
#endif

#ifdef __XID_CTRL_Z__
void Protocol76::parseBugReport(NetworkMessage &msg)
{
	std::string report = msg.GetString();

	IOPlayer::instance()->reportMessage(player->getName() + " has reported: " + report);
	player->sendTextMessage(MSG_INFO_DESCR, "Thanks for your report.");
}
#endif

#ifdef __XID_CTRL_Y__
void Protocol76::parseBanPlayer(NetworkMessage &msg)
{
	std::string name = msg.GetString();
	int reason = msg.GetByte();
	std::string banComment = msg.GetString();
	int banType = msg.GetByte();
	bool isIpBan = msg.GetByte();
	
	Player* banPlayer = g_game.getPlayerByName(name.c_str());
	if(player && banPlayer){
		std::string banReason;
		unsigned long banTime = 7*86400;

		if(player->getAccessLevel() > banPlayer->getAccessLevel()){
 			if(banComment.size() > 0 && banComment.size() < 9999){
				switch(reason){
					case 0:
						banReason = "offensive name";
						break;
					case 1:
						banReason = "name containing part of sentence";
						break;
					case 2:
						banReason = "name with nonsensical letter combo";
						break;
					case 3:
						banReason = "invalid name format";
						break;
					case 4:
						banReason = "name not describing person";
						break;
					case 5:
						banReason = "name of celebrity";
						break;
					case 6:
						banReason = "name referring to country";
						break;
					case 7:
						banReason = "namefaking player identity";
						break;
					case 8:
						banReason = "namefaking official position";
						break;
					case 9:
						banReason = "offensive statement";
						break;
					case 10:
						banReason = "spamming";
						break;
					case 11:
						banReason = "advertisement not related to game";
						break;
					case 12:
						banReason = "real money advertisement";
						break;
					case 13:
						banReason = "Non-English public statement";
						break;
					case 14:
						banReason = "off-topic public statement";
						break;
					case 15:
						banReason = "inciting rule violation";
						break;
					case 16:
						banReason = "bug abuse";
						break;
					case 17:
						banReason = "game weakness abuse";
						break;
					case 18:
						banReason = "using macro";
						break;
					case 19:
						banReason = "using unofficial software to play";
						break;
					case 20:
						banReason = "hacking";
						break;
					case 21:
						banReason = "multi-clienting";
						break;
					case 22:
						banReason = "account trading";
						break;
					case 23:
						banReason = "account sharing";
						break;
					case 24:
						banReason = "threatening gamemaster";
						break;
					case 25:
						banReason = "pretending to have official position";
						break;
					case 26:
						banReason = "pretending to have influence on gamemaster";
						break;
					case 27:
						banReason = "false report to gamemaster";
						break;
					case 28:
						banReason = "excessive unjustifed player killing";
						break;
					case 29:
						banReason = "destructive behaviour";
						break;
					case 30:
						banReason = "spoiling auction";
						break;
					case 31:
						banReason = "invalid payment";
						break;
					default:
						banReason = "nothing";
						break;
				}

				if(isIpBan && banPlayer->getIP() != player->getIP()){
					if(player->getAccessLevel() >= ACCESS_PROTECT){
						g_bans.addIpBan(banPlayer->lastip, 0xFFFFFFFF, std::time(NULL) + banTime, banReason);
						player->sendTextMessage(MSG_INFO_DESCR, banPlayer->getName() + " has been IP banished for 7 days because of " + banReason + ".");
						banPlayer->kickPlayer();
					}
					else{
						player->sendTextMessage(MSG_INFO_DESCR, "You cannot ban players.");
					}
				}
				else{
					if(banType < 2){
						banType = 1;
					}
					else{
						banType = 2;
					}
					
					if(player->getAccessLevel() >= ACCESS_PROTECT){
						if(banType == 1){
                            time_t banEnd = std::time(NULL) + banTime;
							g_bans.addPlayerBan(banPlayer->getName(), banEnd, banReason);
						}
						else{
							g_bans.addAccountBan(banPlayer->getAccount(), std::time(NULL) + banTime, banReason);
						}
						
						player->sendTextMessage(MSG_INFO_DESCR, banPlayer->getName() + " has been banished for 7 days because of " + banReason + ".");
						banPlayer->kickPlayer();
					}
					else{
						IOPlayer::instance()->reportMessage(player->getName() + " has reported " + banPlayer->getName() + " because of " + banReason + ".");
						player->sendTextMessage(MSG_INFO_DESCR, "You have reported " + banPlayer->getName() + " because of " + banReason + ".");
					}
				}
			}
			else{
				player->sendTextMessage(MSG_INFO_DESCR, "Your comment was either to long or not long enough.");
			}
		}
		else{
			player->sendTextMessage(MSG_INFO_DESCR, "You can't ban someone which has a higher access than you.");
		}
	}
	else{
		player->sendTextMessage(MSG_INFO_DESCR, "This player is not online.");
	}
}
#endif
