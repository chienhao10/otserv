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

#include "configmanager.h"


ConfigManager::ConfigManager()
{
	m_isLoaded = false;
}

ConfigManager::~ConfigManager()
{
	//
}

bool ConfigManager::loadFile(const std::string& _filename)
{
	lua_State* L = lua_open();

	if(!L) return false;

	if(luaL_dofile(L, _filename.c_str()))
	{
		lua_close(L);
		return false;
	}

	// parse config
	if(!m_isLoaded) // info that must be loaded one time (unless we reset the modules involved)
	{
		m_confString[CONFIG_FILE] = _filename;
		m_confString[IP] = getGlobalString(L, "ip", "127.0.0.1");
		m_confInteger[PORT] = getGlobalNumber(L, "port");
		m_confString[DATA_DIRECTORY] = getGlobalString(L, "datadir");
		m_confString[MAP_FILE] = getGlobalString(L, "map");
		m_confString[MAP_STORE_FILE] = getGlobalString(L, "mapstore");
		m_confString[HOUSE_STORE_FILE] = getGlobalString(L, "housestore");
		m_confString[HOUSE_RENT_PERIOD] = getGlobalString(L, "houserentperiod", "monthly");
		m_confString[MAP_KIND] = getGlobalString(L, "mapkind");
		m_confString[BAN_FILE] = getGlobalString(L, "banIdentifier");
		m_confString[WORLD_TYPE] = getGlobalString(L, "worldtype");
		m_confString[SQL_HOST] = getGlobalString(L, "sql_host");
		m_confString[SQL_USER] = getGlobalString(L, "sql_user");
		m_confString[SQL_PASS] = getGlobalString(L, "sql_pass");
		m_confString[SQL_DB] = getGlobalString(L, "sql_db");
		m_confString[SQLITE_DB] = getGlobalString(L, "sqlite_db");
		m_confString[SQL_TYPE] = getGlobalString(L, "sql_type");
		m_confString[MAP_HOST] = getGlobalString(L, "map_host");
		m_confString[MAP_USER] = getGlobalString(L, "map_user");
		m_confString[MAP_PASS] = getGlobalString(L, "map_pass");
		m_confString[MAP_DB] = getGlobalString(L, "map_db");
		
	}
	
	m_confString[LOGIN_MSG] = getGlobalString(L, "loginmsg", "Welcome.");
	m_confString[SERVER_NAME] = getGlobalString(L, "servername");
	m_confString[WORLD_NAME] = getGlobalString(L, "worldname", "OpenTibia");
	m_confString[OWNER_NAME] = getGlobalString(L, "ownername");
	m_confString[OWNER_EMAIL] = getGlobalString(L, "owneremail");
	m_confString[URL] = getGlobalString(L, "url");
	m_confString[LOCATION] = getGlobalString(L, "location");
	m_confInteger[LOGIN_TRIES] = getGlobalNumber(L, "logintries", 3);
	m_confInteger[RETRY_TIMEOUT] = getGlobalNumber(L, "retrytimeout", 30 * 1000);
	m_confInteger[LOGIN_TIMEOUT] = getGlobalNumber(L, "logintimeout", 5 * 1000);
	m_confString[MOTD] = getGlobalString(L, "motd");
	m_confInteger[MOTD_NUM] = getGlobalNumber(L, "motdnum");
	m_confInteger[MAX_PLAYERS] = getGlobalNumber(L, "maxplayers");
	m_confInteger[EXHAUSTED] = getGlobalNumber(L, "exhausted", 1500);
	m_confInteger[EXHAUSTED_HEAL] = getGlobalNumber(L, "exhaustedheal", 1000);
	m_confInteger[EXHAUSTED_ADD] = getGlobalNumber(L, "exhaustedadd", 0);
	m_confInteger[PZ_LOCKED] = getGlobalNumber(L, "pzlocked", 60 * 1000);
	m_confInteger[MIN_ACTIONTIME] = getGlobalNumber(L, "minactioninterval", 200);
	m_confInteger[ALLOW_CLONES] = getGlobalNumber(L, "allowclones", 0);
	m_confInteger[RATE_EXPERIENCE] = getGlobalNumber(L, "expmul", 1);
	m_confInteger[RATE_EXPERIENCE_PVP] = getGlobalNumber(L, "expmulpvp", 1);
	m_confInteger[RATE_SKILL] = getGlobalNumber(L, "skillmul", 1);
	m_confInteger[RATE_LOOT] = getGlobalNumber(L, "lootmul", 1);
	m_confInteger[RATE_MAGIC] = getGlobalNumber(L, "manamul", 1);
	m_confInteger[RATE_SPAWN] = getGlobalNumber(L, "spawnmul", 1);
	m_confInteger[MAX_MESSAGEBUFFER] = getGlobalNumber(L, "messagebuffer", 4);
	m_confString[OTSERV_DB_HOST] = getGlobalString(L, "otserv_db_host", "default_db_host_here");
	m_confInteger[OTSERV_DB_ENABLED] = getGlobalNumber(L, "otserv_db_enabled", 0);
	m_confInteger[MAX_SUMMONS] = getGlobalNumber(L, "maxsummons", 2);
	m_confString[GUILD_SYSTEM] = getGlobalString(L, "guildsystem", "ingame");

	m_confInteger[ACCESS_ENTER] = getGlobalNumber(L, "accessenter", 1);
	m_confInteger[ACCESS_PROTECT] = getGlobalNumber(L, "accessprotect", 3);
	m_confInteger[ACCESS_HOUSE] = getGlobalNumber(L, "accesshouse", 3);
	m_confInteger[ACCESS_TALK] = getGlobalNumber(L, "accesstalk", 1);
	m_confInteger[ACCESS_MOVE] = getGlobalNumber(L, "accessremote", 3);
	m_confInteger[ACCESS_LOOK] = getGlobalNumber(L, "accesslook", 3);

	#ifdef __TLM_SERVER_SAVE__
	m_confInteger[SERVER_SAVE] = getGlobalNumber(L, "autosave", 30)*60*1000;
	#endif
	#ifdef __XID_CVS_MODS__
	m_confInteger[BAN_UNJUST] = getGlobalNumber(L, "banunjust", 1);
	m_confInteger[RED_UNJUST] = getGlobalNumber(L, "redunjust", 1);
	m_confInteger[BAN_TIME] = getGlobalNumber(L, "bantime", 1)*60*60;
	m_confInteger[FRAG_TIME] = getGlobalNumber(L, "fragtime", 1)*60*1000;
	m_confInteger[WHITE_TIME] = getGlobalNumber(L, "whitetime", 15)*60*1000;

	m_confString[ITEM_HOTKEYS] = getGlobalString(L, "itemhotkeys", "yes");
	m_confString[BATTLE_WINDOW_PLAYERS] = getGlobalString(L, "battlewindowplayers", "yes");

	m_confInteger[MAX_DEPOT_ITEMS] = getGlobalNumber(L, "maxdepotitems", 1000);
	m_confString[REMOVE_AMMUNATION] = getGlobalString(L, "removeammunation", "yes");
	m_confString[REMOVE_RUNE_CHARGES] = getGlobalString(L, "removerunecharges", "yes");
	#endif
	#ifdef __XID_PREMIUM_SYSTEM__
	m_confString[FREE_PREMIUM] = getGlobalString(L, "freepremium", "no");
	#endif
	#ifdef __PB_BUY_HOUSE__
	m_confInteger[HOUSE_PRICE] = getGlobalNumber(L, "houseprice", 200);
	m_confInteger[HOUSE_LEVEL] = getGlobalNumber(L, "houselevel", 20);
	#endif
	#ifdef __TR_ANTI_AFK__
	m_confInteger[KICK_TIME] = getGlobalNumber(L, "kicktime", 15)*60000;
	#endif
	#ifdef __JD_DEATH_LIST__
	m_confInteger[MAX_DEATH_ENTRIES] = (size_t)getGlobalNumber(L, "maxdeathentries", 10);
	#endif
	#ifdef __XID_CONFIG_CAP__
	m_confString[CAP_SYSTEM] = getGlobalString(L, "capsystem", "yes");
	#endif
	#ifdef __XID_LEARN_SPELLS__
	m_confString[LEARN_SPELLS] = getGlobalString(L, "learnspells", "no");
	#endif
	#ifdef __XID_ACCOUNT_MANAGER__
	m_confString[ACCOUNT_MANAGER] = getGlobalString(L, "accountmanager", "yes");
	#endif
	#ifdef __XID_PROTECTION_SYSTEM__
	m_confInteger[PROTECTION_LIMIT] = getGlobalNumber(L, "protectionlimit", 1);
	#endif
	#ifdef __XID_SUMMONS_FOLLOW__
	m_confString[SUMMONS_FOLLOW] = getGlobalString(L, "summonsfollow", "yes");
	#endif
	#ifdef __XID_PVP_FEATURES__
	m_confString[ALLOW_OUTFIT_CHANGE] = getGlobalString(L, "outfitchange", "yes");
	m_confString[OUTFIT_DAMAGE] = getGlobalString(L, "feetdamage", "yes");
	#endif

	for(int i=0; i<2; ++i){
		m_confCriticalString[i] = atoi(getGlobalStringField(L, "criticaldamage", i+1, "0").c_str());
	}

	m_isLoaded = true;

	lua_close(L);
	return true;
}

bool ConfigManager::reload()
{
	if(!m_isLoaded)
		return false;

	return loadFile(m_confString[CONFIG_FILE]);
}

int ConfigManager::getNumber(int _what)
{
	if(m_isLoaded && _what < LAST_INTEGER_CONFIG)
		return m_confInteger[_what];
	else
	{
		fprintf(stderr, "WARNING: ConfigManager::getNumber()\n");
		return 0;
	}
}
bool ConfigManager::setNumber(int _what, int _value)
{
	if(m_isLoaded && _what < LAST_INTEGER_CONFIG)
	{
		m_confInteger[_what] = _value;
		return true;
	}
	else
	{
		fprintf(stderr, "WARNING: ConfigManager::setNumber()\n");
		return false;
	}
}

std::string ConfigManager::getGlobalString(lua_State* _L, const std::string& _identifier, const std::string& _default)
{
	lua_getglobal(_L, _identifier.c_str());

	if(!lua_isstring(_L, -1))
		return _default;

	int len = (int)lua_strlen(_L, -1);
	std::string ret(lua_tostring(_L, -1), len);
	lua_pop(_L,1);

	return ret;
}

int ConfigManager::getGlobalNumber(lua_State* _L, const std::string& _identifier, const int _default)
{
	lua_getglobal(_L, _identifier.c_str());

	if(!lua_isnumber(_L, -1))
		return _default;

	int val = (int)lua_tonumber(_L, -1);
	lua_pop(_L,1);

	return val;
}

std::string ConfigManager::getGlobalStringField (lua_State* _L, const std::string& _identifier, const int _key, const std::string& _default) {
	lua_getglobal(_L, _identifier.c_str());

	lua_pushnumber(_L, _key);
	lua_gettable(_L, -2);  /* get table[key] */
	if(!lua_isstring(_L, -1))
		return _default;
	std::string result = lua_tostring(_L, -1);
	lua_pop(_L, 2);  /* remove number and key*/
	return result;
}
