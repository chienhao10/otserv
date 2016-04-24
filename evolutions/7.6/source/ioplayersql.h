//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
// Player Loader/Saver based on MySQL
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
#ifndef __IOPLAYERSQL_H
#define __IOPLAYERSQL_H

#include <string>

#include "ioplayer.h"
#include "player.h"
#include "database.h"

/** Baseclass for all Player-Loaders */
class IOPlayerSQL : protected IOPlayer{
public:
	/** Get a textual description of what source is used
	* \returns Name of the source*/
	virtual char* getSourceDescription(){return "Player source: SQL";};
	virtual bool loadPlayer(Player* player, std::string name);

	/** Save a player
	* \returns Wheter the player was successfully saved
	* \param player the player to save
	*/
	virtual bool savePlayer(Player* player);
	virtual bool loadVIP(Player* player);
	virtual bool saveVIP(Player* player);	
	virtual bool playerExists(std::string name);

	bool reportMessage(const std::string& message);

	#ifdef __XID_ACCOUNT_MANAGER__
	virtual bool createPlayer(Player* player);	
	virtual bool getPlayerGuid(Player* player);
	#endif
	
	IOPlayerSQL();
	~IOPlayerSQL(){};

protected:
	std::string getItems(Item* i, int &startid, int startslot, int player, int parentid);
	bool storeNameByGuid(Database &mysql, unsigned long guid);

	struct StringCompareCase{
		bool operator()(const std::string& l, const std::string& r) const{
			if(strcasecmp(l.c_str(), r.c_str()) < 0){
				return true;
			}
			else{
				return false;
			}
		}
	};

	typedef std::map<unsigned long, std::string> NameCacheMap;
	typedef std::map<std::string, unsigned long, StringCompareCase> GuidCacheMap;
	
	NameCacheMap nameCacheMap;
	GuidCacheMap guidCacheMap;
	
	std::string m_host;
	std::string m_user;
	std::string m_pass;
	std::string m_db;
};

#endif
