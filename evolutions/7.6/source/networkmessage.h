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

#ifndef __NETWORK_MESSAGE_H__
#define __NETWORK_MESSAGE_H__


#include "definitions.h"
#include "otsystem.h"
#include "const.h"

class Creature;
class Player;
class Item;
class Position;
class RSA;

class NetworkMessage
{
public:
	// constructor/destructor
	NetworkMessage();
	~NetworkMessage();

	// resets the internal buffer to an empty message
	void Reset();

	// socket functions
	bool ReadFromSocket(SOCKET socket);
	bool WriteToSocket(SOCKET socket);

	// simply read functions for incoming message
	uint8_t  GetByte();
	uint16_t GetU16();
	uint16_t GetSpriteId();
	uint32_t GetU32();
	std::string GetString();
	std::string GetRaw();
	Position GetPosition();

	// skips count unknown/unused bytes in an incoming message
	void SkipBytes(int count);

	// simply write functions for outgoing message
	void AddByte(uint8_t  value);
	void AddU16(uint16_t value);
	void AddU32(uint32_t value);
	void AddBytes(const char* bytes, uint32_t size);

	void AddString(const std::string &value);
	void AddString(const char* value);


	// write functions for complex types
	void AddPosition(const Position &pos);
	void AddItem(uint16_t id, uint8_t count);
	void AddItem(const Item *item);
	void AddItemId(const Item *item);
	void AddItemId(uint16_t itemId);
	void AddCreature(const Creature *creature, bool known, unsigned int remove);

  	int getMessageLength(){
		return m_MsgSize;
	}
	
	bool empty() const { return m_MsgSize == 0;}
	void JoinMessages(NetworkMessage &add);

protected:
	inline bool canAdd(int size){
    	return (size + m_ReadPos < NETWORKMESSAGE_MAXSIZE - 16);
  	};

  	int m_MsgSize;
  	int m_ReadPos;

	uint8_t m_MsgBuf[NETWORKMESSAGE_MAXSIZE];
};


#endif // #ifndef __NETWORK_MESSAGE_H__
