//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
// 
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
#include "tools.h"
#include "house.h"
#include "housetile.h"
#include "spells.h"
#include "combat.h"
#include "commands.h"
#include "monsters.h"
#include "configmanager.h"

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

#include <sstream>

extern Game g_game;
extern Monsters g_monsters;
extern Vocations g_vocations;
extern ConfigManager g_config;
extern Spells* g_spells;

Spells::Spells():
m_scriptInterface("Spell Interface")
{
	m_scriptInterface.initState();
}

Spells::~Spells()
{
	clear();
}

TalkActionResult_t Spells::playerSaySpell(Player* player, SpeakClasses type, const std::string& words)
{
	std::string str_words = words;

	//strip trailing spaces
	trim_left(str_words, " ");
	trim_right(str_words, " ");

	InstantSpell* instantSpell = getInstantSpell(str_words);
	if(!instantSpell){
		return TALKACTION_CONTINUE;
	}

	#ifdef __XID_LEARN_SPELLS__
	if(g_config.getString(ConfigManager::LEARN_SPELLS) == "yes"){
		if(!player->knowsSpell(instantSpell->getWords()) && instantSpell->getManaCost(player) != 0){
			player->sendCancel("You must learn this spell first.");
			g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
			return TALKACTION_FAILED;
		}
	}
	#endif

	std::string param = "";

	if(instantSpell->getHasParam()){
		size_t spellLen = instantSpell->getWords().length();
		size_t paramLen = str_words.length() - spellLen;
		std::string paramText = str_words.substr(spellLen, paramLen);

		if(!paramText.empty() && paramText[0] == ' '){
			size_t loc1 = paramText.find('"', 0);
			size_t loc2 = std::string::npos;
			if(loc1 != std::string::npos){
				loc2 = paramText.find('"', loc1 + 1);
			}

			if(loc2 == std::string::npos){
				loc2 = paramText.length();
			}
			
			param = paramText.substr(loc1 + 1, loc2 - loc1 - 1);

			trim_left(param, " ");
			trim_right(param, " ");
		}
	}

	if(instantSpell->playerCastInstant(player, param)){
		return TALKACTION_BREAK;
	}
	else{
		return TALKACTION_FAILED;
	}
}

void Spells::clear()
{
	RunesMap::iterator it;
	for(it = runes.begin(); it != runes.end(); ++it){
		delete it->second;
	}
	runes.clear();
	
	InstantsMap::iterator it2;
	for(it2 = instants.begin(); it2 != instants.end(); ++it2){
		delete it2->second;
	}
	instants.clear();
}	

LuaScriptInterface& Spells::getScriptInterface()
{
	return m_scriptInterface;	
}

std::string Spells::getScriptBaseName()
{
	return "spells";	
}

Event* Spells::getEvent(const std::string& nodeName)
{
	if(nodeName == "rune"){
		return new RuneSpell(&m_scriptInterface);
	}
	else if(nodeName == "instant"){
		return new InstantSpell(&m_scriptInterface);
	}
	else if(nodeName == "conjure"){
		return new ConjureSpell(&m_scriptInterface);
	}
	else{
		return NULL;
	}
}

bool Spells::registerEvent(Event* event, xmlNodePtr p)
{
	InstantSpell* instant = dynamic_cast<InstantSpell*>(event);
	RuneSpell* rune = dynamic_cast<RuneSpell*>(event);
	if(!instant && !rune)
		return false;
	
	if(instant){
		instants[instant->getWords()] = instant;
	}
	else if(rune){
		runes[rune->getRuneItemId()] = rune;
	}
	else{
		return false;
	}
	
	return true;
}

Spell* Spells::getSpellByName(const std::string& name)
{
	Spell* spell;

	if(spell = getRuneSpellByName(name)){
		return spell;
	}

	if(spell = getInstantSpellByName(name)){
		return spell;
	}

	return NULL;
}

RuneSpell* Spells::getRuneSpell(uint32_t id)
{
	RunesMap::iterator it = runes.find(id);
	if(it != runes.end()){
		return it->second;
	}

	return NULL;
}

RuneSpell* Spells::getRuneSpellByName(const std::string& name)
{
	for(RunesMap::iterator it = runes.begin(); it != runes.end(); ++it){
		if(strcasecmp(it->second->getName().c_str(), name.c_str()) == 0){
			return it->second;
		}
	}

	return NULL;
}
	
InstantSpell* Spells::getInstantSpell(const std::string words)
{
	InstantSpell* result = NULL;
	for(InstantsMap::iterator it = instants.begin(); it != instants.end(); ++it){
		InstantSpell* instantSpell = it->second;
		size_t spellLen = instantSpell->getWords().length();

		if(strncasecmp(instantSpell->getWords().c_str(), words.c_str(), spellLen) == 0){
			if(!result || spellLen > result->getWords().length()){
				result = instantSpell;
			}
		}
	}

	if(result){
		if(words.length() > result->getWords().length() && !result->getHasParam()){
			size_t spellLen = result->getWords().length();
			size_t paramLen = words.length() - spellLen;
			std::string paramText = words.substr(spellLen, paramLen);

			if(paramText[0] != ' '){
				return NULL;
			}
		}

		return result;
	}

	return NULL;
}

InstantSpell* Spells::getInstantSpellByName(const std::string& name)
{
	for(InstantsMap::iterator it = instants.begin(); it != instants.end(); ++it){
		if(strcasecmp(it->second->getName().c_str(), name.c_str()) == 0){
			return it->second;
		}
	}

	return NULL;
}

Position Spells::getCasterPosition(Creature* creature, Direction dir)
{
	Position pos = creature->getPosition();

	switch(dir){
		case NORTH:
			pos.y -= 1;
			break;

		case SOUTH:
			pos.y += 1;
			break;

		case EAST:
			pos.x += 1;
			break;

		case WEST:
			pos.x -= 1;
			break;
		
		case SOUTHWEST:
			pos.x -= 1;
			pos.y += 1;
		break;

		case NORTHWEST:
			pos.x -= 1;
			pos.y -= 1;
		break;

		case NORTHEAST:
			pos.x += 1;
			pos.y -= 1;
		break;

		case SOUTHEAST:
			pos.x += 1;
			pos.y += 1;
		break;

		default:
			break;
	}

	return pos;
}


CombatSpell::CombatSpell(Combat* _combat, bool _needTarget, bool _needDirection)
{
	combat =_combat;
	needTarget = _needTarget;
	needDirection = _needDirection;
}

bool CombatSpell::castSpell(Creature* creature)
{
	Position pos;

	if(needDirection){
		pos = Spells::getCasterPosition(creature, creature->getDirection());
	}
	else{
		pos = creature->getPosition();
	}

	combat->doCombat(creature, pos);
	return true;
}

bool CombatSpell::castSpell(Creature* creature, Creature* target)
{
	if(combat->hasArea()){
		if(needTarget){
			combat->doCombat(creature, target->getPosition());
		}
		else{
			return castSpell(creature);
		}
	}
	else{
		combat->doCombat(creature, target);
	}

	return true;
}

Spell::Spell()
{
	level = -1;
	magLevel = -1;
	mana = 0;
	manaPercent = 0;
	soul = 0;
	exhaustion = false;
	needTarget = false;
	selfTarget = false;
	blockingSolid = false;
	blockingCreature = false;
	premium = false;
	enabled = true;
	isAggressive = true;
}
	
bool Spell::configureSpell(xmlNodePtr p)
{
	int intValue;
	std::string strValue;
	if(readXMLString(p, "name", strValue)){
		name = strValue;
	}
	else{
		std::cout << "Error: [Spell::configureSpell] Spell without name." << std::endl;
		return false;
	}

	if(readXMLInteger(p, "lvl", intValue)){
	 	level = intValue;
	}

	if(readXMLInteger(p, "maglv", intValue)){
	 	magLevel = intValue;
	}

	if(readXMLInteger(p, "mana", intValue)){
	 	mana = intValue;
	}

	if(readXMLInteger(p, "manapercent", intValue)){
	 	manaPercent = intValue;
	}

	if(readXMLInteger(p, "soul", intValue)){
	 	soul = intValue;
	}

	if(readXMLInteger(p, "exhaustion", intValue)){
		exhaustion = (intValue == 1);
	}

	if(readXMLInteger(p, "prem", intValue)){
		premium = (intValue == 1);
	}

	if(readXMLInteger(p, "enabled", intValue)){
		enabled = (intValue == 1);
	}

	if(readXMLInteger(p, "needtarget", intValue)){
		needTarget = (intValue == 1);
	}

	if(readXMLInteger(p, "selftarget", intValue)){
		selfTarget = (intValue == 1);
	}

	if(readXMLInteger(p, "blocking", intValue)){
		blockingSolid = (intValue == 1);
		blockingCreature = (intValue == 1);
	}

	if(readXMLString(p, "blocktype", strValue)){
		if(strcasecmp(strValue.c_str(), "all") == 0){
			blockingSolid = true;
			blockingCreature = true;
		}
		else if(strcasecmp(strValue.c_str(), "solid") == 0){
			blockingSolid = true;
		}
		else if(strcasecmp(strValue.c_str(), "creature") == 0){
			blockingCreature = true;
		}
	}

	if(readXMLInteger(p, "aggressive", intValue)){
		isAggressive = (intValue == 1);
	}

	xmlNodePtr vocationNode = p->children;
	while(vocationNode){
		if(xmlStrcmp(vocationNode->name,(const xmlChar*)"vocation") == 0){
			if(readXMLInteger(vocationNode, "id", intValue)){
				if(intValue != -1){
					vocSpellMap[intValue] = true;
				}
			}
		}
		
		vocationNode = vocationNode->next;
	}

	return true;
}

bool Spell::playerSpellCheck(const Player* player) const
{
	if(player->getAccessLevel() >= ACCESS_PROTECT){
		return true;
	}
	
	if(!enabled){
		return false;
	}
	
	if(isAggressive){
		if(player->getAccessLevel() < ACCESS_PROTECT && player->getTile()->isPz()){
			player->sendCancelMessage(RET_ACTIONNOTPERMITTEDINPROTECTIONZONE);
			return false;
		}
	}

	if(player->hasCondition(CONDITION_EXHAUSTED)){
		player->sendCancelMessage(RET_YOUAREEXHAUSTED);
		
		if(isInstant()){
			g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
		}
		
		return false;
	}

	if(!vocSpellMap.empty()){
		if(vocSpellMap.find(player->getVocationId()) == vocSpellMap.end()){
			player->sendCancel("You do not have the required vocation.");
			g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
			return false;
		}
	}

	#ifdef __XID_PREMIUM_SYSTEM__
	if(premium && !player->isPremium()){
		player->sendCancel("You do not have a premium account.");
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
		return false;
	}
	#endif

	if(player->getLevel() < level){
		player->sendCancelMessage(RET_NOTENOUGHLEVEL);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
		return false;
	}

	if(player->getMagicLevel() < magLevel){
		player->sendCancelMessage(RET_NOTENOUGHMAGICLEVEL);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
		return false;
	}

	if(player->getMana() < getManaCost(player)){
		player->sendCancelMessage(RET_NOTENOUGHMANA);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
		return false;
	}

	if(player->getPlayerInfo(PLAYERINFO_SOUL) < soul){		
		player->sendCancelMessage(RET_NOTENOUGHSOUL);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
		return false;
	}
	
	return true;
}

bool Spell::playerInstantSpellCheck(const Player* player, const Position& toPos)
{
	if(!playerSpellCheck(player)){
		return false;
	}

	if(toPos.x != 0xFFFF){
		const Position& playerPos = player->getPosition();
		if(playerPos.z > toPos.z){
			player->sendCancelMessage(RET_FIRSTGOUPSTAIRS);
			g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
			return false;
		}
		else if(playerPos.z < toPos.z){
			player->sendCancelMessage(RET_FIRSTGODOWNSTAIRS);
			g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
			return false;
		}
		else{
			Tile* tile = g_game.getTile(toPos.x, toPos.y, toPos.z);

			if(!tile){
				player->sendCancelMessage(RET_NOTPOSSIBLE);
				g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
				return false;
			}

			ReturnValue ret;
			if((ret = Combat::canDoCombat(player, tile, isAggressive)) != RET_NOERROR){
				player->sendCancelMessage(ret);
				g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
				return false;
			}
			
			if(blockingCreature && !tile->creatures.empty()){
				player->sendCancelMessage(RET_NOTENOUGHROOM);
				g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
				return false;
			}
			
			if(blockingSolid && tile->hasProperty(BLOCKSOLID)){
				player->sendCancelMessage(RET_NOTENOUGHROOM);
				g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
				return false;
			}
		}
	}

	return true;
}

bool Spell::playerRuneSpellCheck(const Player* player, const Position& toPos)
{
	if(!playerSpellCheck(player)){
		return false;
	}

	if(toPos.x != 0xFFFF){
		const Position& playerPos = player->getPosition();
		if(playerPos.z > toPos.z){
			player->sendCancelMessage(RET_FIRSTGOUPSTAIRS);
			g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
			return false;
		}
		else if(playerPos.z < toPos.z){
			player->sendCancelMessage(RET_FIRSTGODOWNSTAIRS);
			g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
			return false;
		}
		else{
			Tile* tile = g_game.getTile(toPos.x, toPos.y, toPos.z);

			if(!tile){
				player->sendCancelMessage(RET_NOTPOSSIBLE);
				g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
				return false;
			}

			ReturnValue ret;
			if((ret = Combat::canDoCombat(player, tile, isAggressive)) != RET_NOERROR){
				player->sendCancelMessage(ret);
				g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
				return false;
			}

			if(blockingCreature && !tile->creatures.empty()){
				player->sendCancelMessage(RET_NOTENOUGHROOM);
				g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
				return false;
			}
			else if(blockingSolid && tile->hasProperty(BLOCKSOLID)){
				player->sendCancelMessage(RET_NOTENOUGHROOM);
				g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
				return false;
			}
			
			if(needTarget && tile->creatures.empty()){
				player->sendCancelMessage(RET_CANONLYUSETHISRUNEONCREATURES);
				g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
				return false;
			}
		}
	}

	return true;
}

void Spell::postCastSpell(Player* player) const
{
	if(player->getAccessLevel() >= ACCESS_PROTECT){
		return;
	}

	if(exhaustion && isAggressive){
		player->addExhaustionTicks();
	}
	else if(exhaustion && !isAggressive){
		player->addHealExhaustionTicks();
	}
	
	if(isAggressive){
		player->addInFightTicks();
	}

	int32_t manaCost = getManaCost(player);
	int32_t soulCost = getSoulCost(player);
	postCastSpell(player, manaCost, soulCost);
}

void Spell::postCastSpell(Player* player, uint32_t manaCost, uint32_t soulCost) const
{
	if(player->getAccessLevel() >= ACCESS_PROTECT){
		return;
	}

	if(manaCost > 0){
		player->changeMana(-(int32_t)manaCost);
		player->addManaSpent(manaCost);
	}

	if(soulCost > 0){
		player->changeSoul(-(int32_t)soulCost);
	}
}


int32_t Spell::getManaCost(const Player* player) const
{
	if(mana != 0){
		return mana;
	}

	if(manaPercent != 0){
		int32_t maxMana = player->getMaxMana();
		int32_t manaCost = (maxMana * manaPercent)/100;
		return manaCost;
	}

	return 0;
}

int32_t Spell::getSoulCost(const Player* player) const
{
	if(soul != 0){
		return soul;
	}

	return 0;
}

ReturnValue Spell::CreateIllusion(Creature* creature, const Outfit_t outfit, int32_t time)
{
	ConditionOutfit* outfitCondition = new ConditionOutfit(CONDITIONID_COMBAT, CONDITION_OUTFIT, time);

	if(!outfitCondition){
		return RET_NOTPOSSIBLE;
	}

	outfitCondition->addOutfit(outfit);
	creature->addCondition(outfitCondition);

	return RET_NOERROR;
}

ReturnValue Spell::CreateIllusion(Creature* creature, const std::string& name, int32_t time)
{
	uint32_t mId = g_monsters.getIdByName(name);

	if(mId == 0){
		return RET_CREATUREDOESNOTEXIST;
	}

	const MonsterType* mType = g_monsters.getMonsterType(mId);

	if(mType == NULL){
		return RET_CREATUREDOESNOTEXIST;
	}

	if(creature->getPlayer() && creature->getPlayer()->getAccessLevel() < ACCESS_PROTECT){
		if(!mType->isIllusionable){
			return RET_NOTPOSSIBLE;
		}
	}

	/*
	ConditionOutfit* outfitCondition = new ConditionOutfit(CONDITIONID_COMBAT, CONDITION_OUTFIT, time);

	if(!outfitCondition){
		return RET_NOTPOSSIBLE;
	}

	outfitCondition->addOutfit(mType->outfit);
	creature->addCondition(outfitCondition);
	*/

	return CreateIllusion(creature, mType->outfit, time);
}

ReturnValue Spell::CreateIllusion(Creature* creature, uint32_t itemId, int32_t time)
{
	const ItemType& it = Item::items[itemId];
	if(it.id == 0){
		return RET_NOTPOSSIBLE;
	}

	/*
	ConditionOutfit* outfitCondition = new ConditionOutfit(CONDITIONID_COMBAT, CONDITION_OUTFIT, time);

	if(!outfitCondition){
		return RET_NOTPOSSIBLE;
	}

	Outfit_t outfit;
	outfit.lookTypeEx = itemId;
	outfitCondition->addOutfit(outfit);
	creature->addCondition(outfitCondition);

	return RET_NOERROR;
	*/

	Outfit_t outfit;
	outfit.lookTypeEx = itemId;

	return CreateIllusion(creature, outfit, time);
}

InstantSpell::InstantSpell(LuaScriptInterface* _interface) :
TalkAction(_interface)
{
	needDirection = false;
	hasParam = false;
	function = NULL;
}

InstantSpell::~InstantSpell()
{
	//
}

std::string InstantSpell::getScriptEventName()
{
	return "onCastSpell";
}

bool InstantSpell::configureEvent(xmlNodePtr p)
{
	if(!Spell::configureSpell(p)){
		return false;
	}

	if(!TalkAction::configureEvent(p)){
		return false;
	}

	int intValue;
	
	if(readXMLInteger(p, "params", intValue)){
		if(intValue == 1)
	 		hasParam = true;
	}

	if(readXMLInteger(p, "direction", intValue)){
		needDirection = (intValue == 1);
	}

	return true;
}

bool InstantSpell::loadFunction(const std::string& functionName)
{
	if(strcasecmp(functionName.c_str(), "editHouseGuest") == 0){
		isAggressive = false;
		function = HouseGuestList;
	}
	else if(strcasecmp(functionName.c_str(), "editHouseSubOwner") == 0){
		isAggressive = false;
		function = HouseSubOwnerList;
	}
	else if(strcasecmp(functionName.c_str(), "editHouseDoor") == 0){
		isAggressive = false;
		function = HouseDoorList;	
	}
	else if(strcasecmp(functionName.c_str(), "houseKick") == 0){
		isAggressive = false;
		function = HouseKick;
	}
	else if(strcasecmp(functionName.c_str(), "searchPlayer") == 0){
		isAggressive = false;
		function = SearchPlayer;
	}
	else if(strcasecmp(functionName.c_str(), "summonMonster") == 0){
		isAggressive = false;
		function = SummonMonster;
	}
	else if(strcasecmp(functionName.c_str(), "Levitate") == 0){
		isAggressive = false;
		function = Levitate;
	}
	else if(strcasecmp(functionName.c_str(), "illusion") == 0){
		isAggressive = false;
		function = Illusion;
	}
	else{
		return false;
	}
	
	m_scripted = false;
	return true;
}

bool InstantSpell::playerCastInstant(Player* player, const std::string& param)
{
	if(!playerSpellCheck(player)){
		return false;
	}

	LuaVariant var;

	if(selfTarget){
		var.type = VARIANT_NUMBER;
		var.number = player->getID();
	}
	else if(needTarget){
		Player* target = g_game.getPlayerByName(param);
		if(!target || target->getHealth() <= 0){
			player->sendCancelMessage(RET_PLAYERWITHTHISNAMEISNOTONLINE);
			g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
			return false;
		}

		if(!g_game.canThrowObjectTo(player->getPosition(), target->getPosition())){
			player->sendCancelMessage(RET_PLAYERISNOTREACHABLE);
			g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
			return false;
		}

		var.type = VARIANT_NUMBER;
		var.number = target->getID();
	}
	else if(hasParam){
		var.type = VARIANT_STRING;
		var.text = param;
	}
	else{
		var.type = VARIANT_POSITION;
		if(needDirection){
			var.pos = Spells::getCasterPosition(player, player->getDirection());
		}
		else{
			var.pos = player->getPosition();
		}

		if(!playerInstantSpellCheck(player, var.pos)){
			return false;
		}
	}

	bool result = internalCastSpell(player, var);

	if(result){
		Spell::postCastSpell(player);
	}

	return result;
}

bool InstantSpell::castSpell(Creature* creature)
{
	LuaVariant var;
	var.type = VARIANT_POSITION;
	if(needDirection){
		var.pos = Spells::getCasterPosition(creature, creature->getDirection());
	}
	else{
		var.pos = creature->getPosition();
	}

	return internalCastSpell(creature, var);
}

bool InstantSpell::castSpell(Creature* creature, Creature* target)
{
	if(needTarget){
		LuaVariant var;
		var.type = VARIANT_NUMBER;
		var.number = target->getID();

		return internalCastSpell(creature, var);
	}
	else{
		return castSpell(creature);
	}
}

bool InstantSpell::internalCastSpell(Creature* creature, const LuaVariant& var)
{
	bool result = false;
	
	if(m_scripted){
		result =  executeCastSpell(creature, var);
	}
	else{
		if(function){
			result = function(this, creature, var.text);
		}
	}

	return result;
}

bool InstantSpell::executeCastSpell(Creature* creature, const LuaVariant& var)
{
	//onCastSpell(cid, var)
	if(m_scriptInterface->reserveScriptEnv()){
		ScriptEnviroment* env = m_scriptInterface->getScriptEnv();

		#ifdef __DEBUG_LUASCRIPTS__
		std::stringstream desc;
		desc << "onCastSpell - " << creature->getName();
		env->setEventDesc(desc.str());
		#endif
	
		env->setScriptId(m_scriptId, m_scriptInterface);
		env->setRealPos(creature->getPosition());
	
		lua_State* L = m_scriptInterface->getLuaState();

		uint32_t cid = env->addThing(creature);

		m_scriptInterface->pushFunction(m_scriptId);
		lua_pushnumber(L, cid);
		m_scriptInterface->pushVariant(L, var);

		int32_t result = m_scriptInterface->callFunction(2);
		m_scriptInterface->releaseScriptEnv();
		
		return (result == LUA_NO_ERROR);
	}
	else{
		std::cout << "[Error] Call stack overflow. InstantSpell::executeCastSpell" << std::endl;
		return false;
	}
}

House* InstantSpell::getHouseFromPos(Creature* creature)
{
	if(creature){
		Player* player = creature->getPlayer();
		if(player){
			HouseTile* houseTile = dynamic_cast<HouseTile*>(player->getTile());
			if(houseTile){
				House* house = houseTile->getHouse();
				if(house){
					return house;
				}
			}
		}
	}
	return NULL;
}

bool InstantSpell::HouseGuestList(const InstantSpell* spell, Creature* creature, const std::string& param)
{
	House* house = getHouseFromPos(creature);
	if(!house)
		return false;
	Player* player = creature->getPlayer();
	
	if(house->canEditAccessList(GUEST_LIST, player)){
		player->sendHouseWindow(house, GUEST_LIST);
		return true;
	}
	else{
		player->sendCancelMessage(RET_NOTPOSSIBLE);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
	}
	
	return true;
}

bool InstantSpell::HouseSubOwnerList(const InstantSpell* spell, Creature* creature, const std::string& param)
{
	House* house = getHouseFromPos(creature);
	if(!house)
		return false;
	
	Player* player = creature->getPlayer();
	
	if(house->canEditAccessList(SUBOWNER_LIST, player)){
		player->sendHouseWindow(house, SUBOWNER_LIST);
		return true;
	}
	else{
		player->sendCancelMessage(RET_NOTPOSSIBLE);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
	}
		
	return true;
}

bool InstantSpell::HouseDoorList(const InstantSpell* spell, Creature* creature, const std::string& param)
{
	House* house = getHouseFromPos(creature);
	if(!house)
		return false;
	
	Player* player = creature->getPlayer();
	Position pos = player->getPosition();

	switch(player->getDirection()){
	case NORTH:
		pos.y -= 1;
		break;
	case SOUTH:
		pos.y += 1;
		break;
	case WEST:
		pos.x -= 1;
		break;
	case EAST:
		pos.x += 1;
		break;
	default:
		break;
	}

	Door* door = house->getDoorByPosition(pos);
	if(door && house->canEditAccessList(door->getDoorId(), player)){
		player->sendHouseWindow(house, door->getDoorId());
		return true;
	}
	else{
		player->sendCancelMessage(RET_NOTPOSSIBLE);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
	}
	
	return true;
}

bool InstantSpell::HouseKick(const InstantSpell* spell, Creature* creature, const std::string& param)
{
	House* house = getHouseFromPos(creature);
	if(!house)
		return false;
	
	Player* player = creature->getPlayer();
	house->kickPlayer(player, param);
	
	return true;
}

bool InstantSpell::SearchPlayer(const InstantSpell* spell, Creature* creature, const std::string& param)
{
	//a. From 1 to 4 sq's [Person] is standing next to you.
	//b. From 5 to 100 sq's [Person] is to the south, north, east, west.
	//c. From 101 to 274 sq's [Person] is far to the south, north, east, west.
	//d. From 275 to infinite sq's [Person] is very far to the south, north, east, west.
	//e. South-west, s-e, n-w, n-e (corner coordinates): this phrase appears if the player you're looking for has moved five squares in any direction from the south, north, east or west.
	//f. Lower level to the (direction): this phrase applies if the person you're looking for is from 1-25 squares up/down the actual floor you're in.
	//g. Higher level to the (direction): this phrase applies if the person you're looking for is from 1-25 squares up/down the actual floor you're in.

	Player* player = creature->getPlayer();
	if(!player){
		return false;
	}

	enum distance_t{
		DISTANCE_BESIDE,
		DISTANCE_CLOSE_1,
		DISTANCE_CLOSE_2,
		DISTANCE_FAR,
		DISTANCE_VERYFAR,
	};
	
	enum direction_t{
		DIR_N, DIR_S, DIR_E, DIR_W,
		DIR_NE, DIR_NW, DIR_SE, DIR_SW,
	};
	
	enum level_t{
		LEVEL_HIGHER,
		LEVEL_LOWER,
		LEVEL_SAME,
	};

	Player* playerExiva = g_game.getPlayerByName(param);
	if(playerExiva){
		const Position lookPos = player->getPosition();
		const Position searchPos = playerExiva->getPosition();
		
		long dx = lookPos.x - searchPos.x;
		long dy = lookPos.y - searchPos.y;
		long dz = lookPos.z - searchPos.z;
		
		distance_t distance;
		direction_t direction;
		level_t level;
		//getting floor
		if(dz > 0){
			level = LEVEL_HIGHER;
		}
		else if(dz < 0){
			level = LEVEL_LOWER;
		}
		else{
			level = LEVEL_SAME;
		}
		//getting distance
		if(std::abs(dx) < 4 && std::abs(dy) <4){
			distance = DISTANCE_BESIDE;
		}
		else{
			long distance2 = dx*dx + dy*dy;
			if(distance2 < 625){
				distance = DISTANCE_CLOSE_1;
			}
			else if(distance2 < 10000){
				distance = DISTANCE_CLOSE_2;
			}
			else if(distance2 < 75076){
				distance = DISTANCE_FAR;
			}
			else{
				distance = DISTANCE_VERYFAR;
			}
		}
		//getting direction
		float tan;
		if(dx != 0){
			tan = (float)dy/(float)dx;
		}
		else{
			tan = 10.;
		}
		if(std::abs(tan) < 0.4142){
			if(dx > 0){
				direction = DIR_W;
			}
			else{
				direction = DIR_E;
			}			
		}
		else if(std::abs(tan) < 2.4142){
			if(tan > 0){
				if(dy > 0){
					direction = DIR_NW;
				}
				else{
					direction = DIR_SE;
				}
			}
			else{ //tan < 0
				if(dx > 0){
					direction = DIR_SW;
				}
				else{
					direction = DIR_NE;
				}
			}
		}
		else{
			if(dy > 0){
				direction = DIR_N;
			}
			else{
				direction = DIR_S;
			}
		}
		
		std::stringstream ss;
		ss << playerExiva->getName() << " ";

		if(distance == DISTANCE_BESIDE){
			if(level == LEVEL_SAME)
				ss << "is standing next to you";
			else if(level == LEVEL_HIGHER)
				ss << "is above you";
			else if(level == LEVEL_LOWER)
				ss << "is below you";
			}
		else{
			switch(distance){
			case DISTANCE_CLOSE_1:
				if(level == LEVEL_SAME){
					ss << "is to the";
				}
				else if(level == LEVEL_HIGHER){
					ss << "is on a higher level to the";
				}
				else if(level == LEVEL_LOWER){
					ss << "is on a lower level to the";
				}
				break;
				
			case DISTANCE_CLOSE_2:
				ss << "is to the";
				break;

			case DISTANCE_FAR:
				ss << "is far to the";
				break;

			case DISTANCE_VERYFAR:
				ss << "is very far to the";
				break;
			
			default:
				break;
			}

			ss << " ";
			switch(direction){
				case DIR_N:
					ss << "north";
					break;

				case DIR_S:
					ss << "south";
					break;
				case DIR_E:

					ss << "east";
					break;

				case DIR_W:
					ss << "west";
					break;

				case DIR_NE:
					ss << "north-east";
					break;

				case DIR_NW:
					ss << "north-west";
					break;

				case DIR_SE:
					ss << "south-east";
					break;

				case DIR_SW:
					ss << "south-west";
					break;
			}
		}

		ss << ".";
		player->sendTextMessage(MSG_INFO_DESCR, ss.str().c_str());
		g_game.addMagicEffect(player->getPosition(), NM_ME_MAGIC_ENERGY);
		return true;
	}
	else{
		player->sendCancelMessage(RET_PLAYERWITHTHISNAMEISNOTONLINE);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
	}

	return false;
}

bool InstantSpell::SummonMonster(const InstantSpell* spell, Creature* creature, const std::string& param)
{
	Player* player = creature->getPlayer();
	if(!player){
		return false;
	}

	MonsterType* mType = g_monsters.getMonsterType(param);
	if(!mType){
		player->sendCancelMessage(RET_NOTPOSSIBLE);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
		return false;
	}

	if(player->getAccessLevel() < ACCESS_PROTECT && !mType->isSummonable){
		player->sendCancelMessage(RET_NOTPOSSIBLE);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
		return false;
	}

	int32_t manaCost = mType->manaCost;
	if(player->getMana() < manaCost){
		player->sendCancelMessage(RET_NOTENOUGHMANA);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
		return false;
	}

	if(player->getSummonCount() >= g_config.getNumber(ConfigManager::MAX_SUMMONS)){
		player->sendCancel("You cannot summon more creatures.");
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
		return false;
	}

	ReturnValue ret = Commands::placeSummon(creature, param);

	if(ret == RET_NOERROR){
		spell->postCastSpell(player, manaCost, spell->getSoulCost(player));
		g_game.addMagicEffect(player->getPosition(), NM_ME_MAGIC_POISON);
	}
	else{
		player->sendCancelMessage(ret);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
	}

	return (ret == RET_NOERROR);
}

bool InstantSpell::Levitate(const InstantSpell* spell, Creature* creature, const std::string& param)
{
	Player* player = creature->getPlayer();
	if(!player){
		return false;
	}

	const Position& currentPos = creature->getPosition();
	const Position& destPos = Spells::getCasterPosition(creature, creature->getDirection());

	ReturnValue ret = RET_NOTPOSSIBLE;

	if(strcasecmp(param.c_str(), "up") == 0){
		if(currentPos.z != 8){
			Tile* tmpTile = g_game.getTile(currentPos.x, currentPos.y, currentPos.z - 1);
			if(tmpTile == NULL || (tmpTile->ground == NULL && !tmpTile->hasProperty(BLOCKSOLID))){
				tmpTile = g_game.getTile(destPos.x, destPos.y, destPos.z - 1);
				if(tmpTile && tmpTile->ground && !tmpTile->hasProperty(BLOCKSOLID)){
					ret = g_game.internalMoveCreature(player, player->getTile(), tmpTile);
				}
			}
		}
	}
	else if(strcasecmp(param.c_str(), "down") == 0){
		Tile* tmpTile = g_game.getTile(destPos.x, destPos.y, destPos.z);
		if(currentPos.z != 7 && (tmpTile == NULL || (tmpTile->ground == NULL && !tmpTile->hasProperty(BLOCKSOLID)))){
			tmpTile = g_game.getTile(destPos.x, destPos.y, destPos.z + 1);

			if(tmpTile){
				ret = g_game.internalMoveCreature(player, player->getTile(), tmpTile);
			}
		}
	}

	if(ret == RET_NOERROR){
		g_game.addMagicEffect(player->getPosition(), NM_ME_ENERGY_AREA);
	}
	else{
		player->sendCancelMessage(ret);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
	}

	return (ret == RET_NOERROR);
}

bool InstantSpell::Illusion(const InstantSpell* spell, Creature* creature, const std::string& param)
{
	Player* player = creature->getPlayer();
	if(!player){
		return false;
	}

	ReturnValue ret = CreateIllusion(creature, param, 60000);

	if(ret == RET_NOERROR){
		g_game.addMagicEffect(player->getPosition(), NM_ME_MAGIC_BLOOD);
	}
	else{
		player->sendCancelMessage(ret);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
	}

	return (ret == RET_NOERROR);
}

ConjureSpell::ConjureSpell(LuaScriptInterface* _interface) :
InstantSpell(_interface)
{
	isAggressive = false;
	conjureId = 0;
	conjureCount = 1;
	conjureReagentId = 0;
}

ConjureSpell::~ConjureSpell()
{
	//
}

std::string ConjureSpell::getScriptEventName()
{
	return "onCastSpell";
}

bool ConjureSpell::configureEvent(xmlNodePtr p)
{
	if(!InstantSpell::configureEvent(p)){
		return false;
	}

	/*
	if(!Spell::configureSpell(p)){
		return false;
	}

	if(!TalkAction::configureEvent(p)){
		return false;
	}
	*/

	int intValue;
	
	if(readXMLInteger(p, "conjureId", intValue)){
		conjureId = intValue;
	}

	if(readXMLInteger(p, "conjureCount", intValue)){
		conjureCount = intValue;
	}

	if(readXMLInteger(p, "reagentId", intValue)){
		conjureReagentId = intValue;
	}	

	return true;
}

bool ConjureSpell::loadFunction(const std::string& functionName)
{
	if(strcasecmp(functionName.c_str(), "conjureItem") == 0){
		function = ConjureItem;
	}
	else if(strcasecmp(functionName.c_str(), "conjureRune") == 0){
		function = ConjureItem;
	}
	else if(strcasecmp(functionName.c_str(), "conjureFood") == 0){
		function = ConjureFood;
	}
	else{
		return false;
	}
	
	m_scripted = false;
	return true;
}

bool ConjureSpell::internalConjureItem(Player* player, uint32_t conjureId, uint32_t conjureCount)
{
	Item* newItem = Item::CreateItem(conjureId, conjureCount);
	if(!newItem){
		return false;
	}

	ReturnValue ret = g_game.internalPlayerAddItem(player, newItem);
	return (ret == RET_NOERROR);
}

bool ConjureSpell::internalConjureItem(Player* player, uint32_t conjureId, uint32_t conjureCount, uint32_t reagentId, slots_t slot)
{
	bool result = false;
	if(reagentId != 0){
		Item* item;

		item = player->getInventoryItem(slot);
		if(item && item->getID() == reagentId){
			
			Item* newItem = g_game.transformItem(item, conjureId, conjureCount);
			g_game.startDecay(newItem);
			result = true;
		}
	}

	return result;
}

bool ConjureSpell::ConjureItem(const ConjureSpell* spell, Creature* creature, const std::string& param)
{
	Player* player = creature->getPlayer();

	if(!player){
		return false;
	}
	
	bool result = false;

	if(spell->getReagentId() != 0){
		
		if(!spell->playerSpellCheck(player)){
			return false;
		}

		if(internalConjureItem(player, spell->getConjureId(), spell->getConjureCount(),
		spell->getReagentId(), SLOT_LEFT)){
			spell->postCastSpell(player);
			result = true;
		}

		if(!spell->playerSpellCheck(player)){
			return false;
		}

		if(internalConjureItem(player, spell->getConjureId(), spell->getConjureCount(),
		spell->getReagentId(), SLOT_RIGHT)){
			spell->postCastSpell(player);
			result = true;
		}
	}
	else{
		if(internalConjureItem(player, spell->getConjureId(), spell->getConjureCount())){
			spell->postCastSpell(player);
			result = true;
		}
	}

	if(result){
		g_game.addMagicEffect(player->getPosition(), NM_ME_MAGIC_BLOOD);
	}
	else if(spell->getReagentId() != 0){
		player->sendCancel("You need a magic item to cast this spell.");
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
	}

	return result;
}

bool ConjureSpell::ConjureFood(const ConjureSpell* spell, Creature* creature, const std::string& param)
{
	Player* player = creature->getPlayer();

	if(!player){
		return false;
	}

	uint32_t foodType = 0;
	switch(rand() % 7){
		case 0: foodType = ITEM_MEAT; break;
		case 1: foodType = ITEM_HAM; break;
		case 2: foodType = ITEM_GRAPE; break;
		case 3: foodType = ITEM_APPLE; break;
		case 4: foodType = ITEM_BREAD; break;
		case 5: foodType = ITEM_CHEESE; break;
		case 6: foodType = ITEM_ROLL; break;
		case 7: foodType = ITEM_BREAD; break;
	}

	bool result = internalConjureItem(player, foodType, 1);

	if(result){
		spell->postCastSpell(player);
		g_game.addMagicEffect(player->getPosition(), NM_ME_MAGIC_POISON);
	}

	return result;
}

bool ConjureSpell::playerCastInstant(Player* player, const std::string& param)
{
	if(!playerSpellCheck(player)){
		return false;
	}

	bool result = false;
	
	if(m_scripted){
		LuaVariant var;
		var.type = VARIANT_STRING;
		var.text = param;
		result =  executeCastSpell(player, var);
	}
	else{
		if(function){
			result = function(this, player, param);
		}
	}

	return result;
}

RuneSpell::RuneSpell(LuaScriptInterface* _interface) :
Action(_interface)
{
	hasCharges = true;
	runeId = 0;
	function = NULL;
}

RuneSpell::~RuneSpell()
{
	//
}

std::string RuneSpell::getScriptEventName()
{
	return "onCastSpell";
}
	
bool RuneSpell::configureEvent(xmlNodePtr p)
{
	if(!Spell::configureSpell(p)){
		return false;
	}
	
	if(!Action::configureEvent(p)){
		return false;
	}

	int intValue;
	if(readXMLInteger(p, "id", intValue)){
		runeId = intValue;
	}
	else{
		std::cout << "Error: [RuneSpell::configureSpell] Rune spell without id." << std::endl;
		return false;
	}

	uint32_t charges = 0;
	if(readXMLInteger(p, "charges", intValue)){
		charges = intValue;
	}

	hasCharges = (charges > 0);

	if(magLevel != -1){
		//Change magic level in the ItemType to get accurate description
		ItemType& iType = Item::items.getItemType(runeId);
		iType.runeMagLevel = magLevel;
		iType.charges = charges;
		iType.runeName = getName();
	}

	return true;
}

bool RuneSpell::loadFunction(const std::string& functionName)
{
	if(strcasecmp(functionName.c_str(), "chameleon") == 0){
		function = Illusion;
	}
	else if(strcasecmp(functionName.c_str(), "convince") == 0){
		function = Convince;
	}
	else{
		return false;
	}
	
	m_scripted = false;
	return true;
}

bool RuneSpell::Illusion(const RuneSpell* spell, Creature* creature, Item* item,
	const Position& posFrom, const Position& posTo)
{
	Player* player = creature->getPlayer();
	if(!player){
		return false;
	}

	Thing* thing = g_game.internalGetThing(player, posTo, STACKPOS_MOVE);
	if(!thing){
		player->sendCancelMessage(RET_NOTPOSSIBLE);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
		return false;
	}

	Item* illusionItem = thing->getItem();
	if(!illusionItem || illusionItem->isNotMoveable()){
		player->sendCancelMessage(RET_NOTPOSSIBLE);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
		return false;
	}

	ReturnValue ret = CreateIllusion(creature, illusionItem->getID(), 60000);

	if(ret == RET_NOERROR){
		g_game.addMagicEffect(player->getPosition(), NM_ME_MAGIC_BLOOD);
	}
	else{
		player->sendCancelMessage(ret);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
	}

	return (ret == RET_NOERROR);
}

bool RuneSpell::Convince(const RuneSpell* spell, Creature* creature, Item* item, const Position& posFrom, const Position& posTo)
{
	Player* player = creature->getPlayer();
	if(!player){
		return false;
	}

	if(player->getAccessLevel() < ACCESS_PROTECT){
		if(player->getSummonCount() >= g_config.getNumber(ConfigManager::MAX_SUMMONS)){
			player->sendCancelMessage(RET_NOTPOSSIBLE);
			g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
			return false;
		}
	}

	Thing* thing = g_game.internalGetThing(player, posTo, 0, 0, STACKPOS_LOOK);
	if(!thing){
		player->sendCancelMessage(RET_NOTPOSSIBLE);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
		return false;
	}

	Creature* convinceCreature = thing->getCreature();
	if(!convinceCreature || !convinceCreature->convinceCreature(creature)){
		player->sendCancelMessage(RET_NOTPOSSIBLE);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
		return false;
	}

	int32_t manaCost = 0;
	
	if(convinceCreature->getMonster()){
		manaCost = convinceCreature->getMonster()->getManaCost();
	}

	if(player->getMana() < manaCost){
		player->sendCancelMessage(RET_NOTENOUGHMANA);
		g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
		return false;
	}

	spell->postCastSpell(player, manaCost, spell->getSoulCost(player));
	g_game.addMagicEffect(player->getPosition(), NM_ME_MAGIC_BLOOD);
	return true;
}

ReturnValue RuneSpell::canExecuteAction(const Player* player, const Position& toPos)
{
	ReturnValue ret = Action::canExecuteAction(player, toPos);
	if(ret != RET_NOERROR){
		return ret;
	}

	if(toPos.x == 0xFFFF){
		if(needTarget){
			return RET_CANONLYUSETHISRUNEONCREATURES;
		}
		else if(!selfTarget){
			return RET_NOTENOUGHROOM;
		}
	}

	return RET_NOERROR;
}

bool RuneSpell::executeUse(Player* player, Item* item, const PositionEx& posFrom, const PositionEx& posTo, bool extendedUse)
{
	if(!playerRuneSpellCheck(player, posTo)){
		return false;
	}

	if(Player* attackedPlayer = dynamic_cast<Player*>(g_game.internalGetThing(player, posTo, 253))){
		if(player->safeMode != 0 && isAggressive == 0 && attackedPlayer->getSkull() != SKULL_RED && attackedPlayer->getSkull() != SKULL_WHITE){
			player->sendTextMessage(MSG_STATUS_SMALL, "Turn secure mode off if you really want to attack an unmarked player.");
			return false;
		}
		else if(attackedPlayer->getVocationId() == 0){
			player->sendCancelMessage(RET_YOUMAYNOTATTACKTHISPLAYER);
			g_game.addMagicEffect(player->getPosition(), NM_ME_PUFF);
			return false;
		}
	}

	bool result = false;

	if(m_scripted){
		LuaVariant var;
		var.type = VARIANT_POSITION;
		var.pos = posTo;

		result = internalCastSpell(player, var);
	}
	else{
		if(function){
			result = function(this, player, item, posFrom, posTo);
		}
	}

	if(result){
		Spell::postCastSpell(player);		
	
    	if(hasCharges && item){
			int32_t newCharge = std::max(0, item->getItemCharge() - 1);
			#ifdef __XID_CVS_MODS__
			if(g_config.getString(ConfigManager::REMOVE_RUNE_CHARGES) == "yes")
			#endif
			g_game.transformItem(item, item->getID(), newCharge);
		}
	}

	return result;
}

bool RuneSpell::castSpell(Creature* creature)
{
	LuaVariant var;
	var.type = VARIANT_NUMBER;
	var.number = creature->getID();

	return internalCastSpell(creature, var);
}

bool RuneSpell::castSpell(Creature* creature, Creature* target)
{
	LuaVariant var;
	var.type = VARIANT_NUMBER;
	var.number = target->getID();

	return internalCastSpell(creature, var);
}

bool RuneSpell::internalCastSpell(Creature* creature, const LuaVariant& var)
{
	bool result = false;
	
	if(m_scripted){
		result = executeCastSpell(creature, var);
	}
	else{
		//call hardcodedAction
		result = false;
	}

	return result;
}

bool RuneSpell::executeCastSpell(Creature* creature, const LuaVariant& var)
{
	//onCastSpell(cid, var)
	if(m_scriptInterface->reserveScriptEnv()){
		ScriptEnviroment* env = m_scriptInterface->getScriptEnv();
		
		#ifdef __DEBUG_LUASCRIPTS__
		std::stringstream desc;
		desc << "onCastSpell - " << creature->getName();
		env->setEventDesc(desc.str());
		#endif
		
		env->setScriptId(m_scriptId, m_scriptInterface);
		env->setRealPos(creature->getPosition());
		
		lua_State* L = m_scriptInterface->getLuaState();
		
		uint32_t cid = env->addThing(creature);

		m_scriptInterface->pushFunction(m_scriptId);
		lua_pushnumber(L, cid);
		m_scriptInterface->pushVariant(L, var);

		int32_t result = m_scriptInterface->callFunction(2);
		m_scriptInterface->releaseScriptEnv();
		
		return (result == LUA_NO_ERROR);
	}
	else{
		std::cout << "[Error] Call stack overflow. RuneSpell::executeCastSpell" << std::endl;
		return false;
	}
}
