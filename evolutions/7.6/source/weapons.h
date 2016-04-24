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


#ifndef __OTSERV_WEAPONS_H__
#define __OTSERV_WEAPONS_H__

#include "game.h"
#include "luascript.h"
#include "player.h"
#include "actions.h"
#include "talkaction.h"
#include "baseevents.h"
#include "combat.h"

class Weapon;
class WeaponMelee;
class WeaponDistance;
class WeaponWand;

enum AmmoAction_t{
	AMMOACTION_NONE,
	AMMOACTION_REMOVECOUNT,
	AMMOACTION_REMOVECHARGE,
	AMMOACTION_MOVE
};

class Weapons : public BaseEvents
{
public:
	Weapons();
	virtual ~Weapons();
	
	bool loadDefaults();
	const Weapon* getWeapon(const Item* item) const;

	static int32_t weaponExhaustionTime;
	static int32_t weaponInFightTime;

	static int32_t getMaxWeaponDamage(int32_t attackSkill, int32_t attackValue);

protected:
	virtual void clear();
	virtual LuaScriptInterface& getScriptInterface();
	virtual std::string getScriptBaseName();
	virtual Event* getEvent(const std::string& nodeName);
	virtual bool registerEvent(Event* event, xmlNodePtr p);
	
	typedef std::map<uint32_t, Weapon*> WeaponMap;
	WeaponMap weapons;
	
	LuaScriptInterface m_scriptInterface;
};

class Weapon : public Event
{
public:
	Weapon(LuaScriptInterface* _interface);
	virtual ~Weapon();

	virtual bool configureEvent(xmlNodePtr p);
	virtual bool configureWeapon(const ItemType& it);

	virtual bool checkLastAction(Player* player, int32_t interval) const {return true;}
	virtual bool playerWeaponCheck(Player* player, Creature* target) const;
	virtual bool useWeapon(Player* player, Item* item, Creature* target) const;

	void setCombatParam(const CombatParams& _params);

	uint16_t getID() const {return id;}

	static bool useFist(Player* player, Creature* target);

protected:
	virtual std::string getScriptEventName();

	bool executeUseWeapon(Player* player, const LuaVariant& var) const;
	bool internalUseWeapon(Player* player, Item* item, Creature* target) const;
	bool internalUseWeapon(Player* player, Item* item, Tile* tile) const;

	//virtual void onUseWeapon(Player* player, Item* item) const;
	virtual void onUsedWeapon(Player* player, Item* item, Tile* destTile) const;
	virtual int32_t getWeaponDamage(const Player* player, const Item* item) const = 0;
	virtual bool getSkillType(const Player* player, const Item* item, skills_t& skill, uint32_t& skillpoint) const {return false;};

	int32_t getManaCost(const Player* player) const;

	uint16_t id;
	bool enabled;
	bool premium;
	bool exhaustion;
	int32_t level;
	int32_t magLevel;
	int32_t mana;
	int32_t manaPercent;
	int32_t soul;
	int32_t range;
	AmmoAction_t ammoAction;
	CombatParams params;
	
	friend class Player;

private:
	typedef std::map<int32_t, bool> VocWeaponMap;
	VocWeaponMap vocWeaponMap;
};

class WeaponMelee : public Weapon
{
public:
	WeaponMelee(LuaScriptInterface* _interface);
	virtual ~WeaponMelee() {};

	virtual bool configureEvent(xmlNodePtr p);
	virtual bool configureWeapon(const ItemType& it);

protected:
	virtual void onUsedWeapon(Player* player, Item* item, Tile* destTile) const;
	virtual int32_t getWeaponDamage(const Player* player, const Item* item) const;
	virtual bool getSkillType(const Player* player, const Item* item, skills_t& skill, uint32_t& skillpoint) const;
};

class WeaponDistance : public Weapon
{
public:
	WeaponDistance(LuaScriptInterface* _interface);
	virtual ~WeaponDistance() {};

	virtual bool configureEvent(xmlNodePtr p);
	virtual bool configureWeapon(const ItemType& it);

	//virtual bool checkLastAction(Player* player, int32_t interval) const {return (OTSYS_TIME() - getLastAction() >= interval);}
	virtual bool checkLastAction(Player* player, int32_t interval) const {return (player->getLastAction() + interval < OTSYS_TIME());}
	virtual bool playerWeaponCheck(Player* player, Creature* target) const;
	virtual bool useWeapon(Player* player, Item* item, Creature* target) const;

protected:
	virtual void onUsedWeapon(Player* player, Item* item, Tile* destTile) const;
	virtual int32_t getWeaponDamage(const Player* player, const Item* item) const;
	virtual bool getSkillType(const Player* player, const Item* item, skills_t& skill, uint32_t& skillpoint) const;

	int32_t hitChance;
	int32_t ammuAttackValue;
};

class WeaponWand : public Weapon
{
public:
	WeaponWand(LuaScriptInterface* _interface);
	virtual ~WeaponWand() {};

	virtual bool configureEvent(xmlNodePtr p);
	virtual bool checkLastAction(Player* player, int32_t interval) const {return (player->getLastAction() + interval < OTSYS_TIME());}

protected:
	virtual int32_t getWeaponDamage(const Player* player, const Item* item) const;
	virtual bool getSkillType(const Player* player, const Item* item, skills_t& skill, uint32_t& skillpoint) const {return false;};

	int32_t minChange;
	int32_t maxChange;
};

#endif
