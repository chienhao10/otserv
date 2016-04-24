//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
// otserv main. The only place where things get instantiated.
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
#include <iomanip>
#include <sstream>

#include "otsystem.h"
#include "networkmessage.h"
#include "protocol76.h"

#include <stdlib.h>
#include <time.h>
#include "game.h"

#include "ioaccount.h"
#include "ioplayer.h"

#include "status.h"
#include "monsters.h"
#include "commands.h"
#include "vocation.h"
#include "scriptmanager.h"
#include "configmanager.h"
#include "account.h"

#include "movement.h"
#include "spells.h"
#include "talkaction.h"
#include "weapons.h"

#include "tools.h"
#include "waitlist.h"
#include "ban.h"
#include "raids.h"

#ifdef __OTSERV_ALLOCATOR__
#include "allocator.h"
#endif

#ifdef WIN32
	#define ERROR_EINTR WSAEINTR
#else
	#include <fcntl.h>
	#include <arpa/inet.h>
	#include <signal.h>

	#define SOCKET_ERROR -1
	#define ERROR_EINTR EINTR
#endif

#ifdef __DEBUG_CRITICALSECTION__
OTSYS_THREAD_LOCK_CLASS::LogList OTSYS_THREAD_LOCK_CLASS::loglist;
#endif


#ifdef BOOST_NO_EXCEPTIONS
	#include <exception>
	void boost::throw_exception(std::exception const & e){
		std::cout << "Boost exception: " << e.what() << std::endl;
	}
#endif

typedef std::vector< std::pair<unsigned long, unsigned long> > IPList;
IPList serverIPs;

ConfigManager g_config;
Actions* g_actions;
Ban g_bans;
Game g_game;
Commands commands(&g_game);
Monsters g_monsters;
MoveEvents* g_moveEvents;
Spells* g_spells;
TalkActions* g_talkactions;
Vocations g_vocations;
Weapons* g_weapons;

#ifdef __JD_BED_SYSTEM__
#include "beds.h"
extern Beds g_beds;
#endif

#if defined USE_SQL_ENGINE
xmlMutexPtr xmlmutex;
#endif

#if defined __EXCEPTION_TRACER__
#include "exception.h"
#endif
#include "networkmessage.h"

bool passwordTest(std::string &plain, std::string &hash)
{
	if(plain == hash){
		return true;
	}
	else{
		return false;
	}
}

OTSYS_THREAD_RETURN ConnectionHandler(void *dat)
{
#if defined __EXCEPTION_TRACER__
	ExceptionHandler playerExceptionHandler;
	playerExceptionHandler.InstallHandler();
#endif

	srand((unsigned)time(NULL));

	SOCKET s = *(SOCKET*)dat;

	NetworkMessage msg;
	if(msg.ReadFromSocket(s)){
		unsigned char protId = msg.GetByte();

		// login server connection
		if(protId == 0x01){
			/*uint16_t clientos =*/ msg.GetU16();
			uint16_t version  = msg.GetU16();
			msg.SkipBytes(12);

			unsigned int accnumber = msg.GetU32();
			std::string  password  = msg.GetString();

			msg.Reset();

			if(version >= CLIENT_VERSION_MIN && version <= CLIENT_VERSION_MAX){
				int serverip = serverIPs[0].first;

				sockaddr_in sain;
				socklen_t salen = sizeof(sockaddr_in);
				if(getpeername(s, (sockaddr*)&sain, &salen) == 0){
					uint32_t clientip = *(uint32_t*)&sain.sin_addr;
					for(unsigned int i = 0; i < serverIPs.size(); i++)
						if((serverIPs[i].first & serverIPs[i].second) == (clientip & serverIPs[i].second)){
							serverip = serverIPs[i].first;
							break;
						}
				}

				if(g_bans.isIpDisabled(s)){
					msg.AddByte(0x0A);
					msg.AddString("Too many connections attempts from this IP. Try again later.");
				}
				else if(g_bans.isIpBanished(s)){
					msg.AddByte(0x0A);
					time_t banEnd = g_bans.getBanEnd(s);
					std::stringstream message;
					if(banEnd != -1){
						message << "Your IP-Address has been banished because of " << g_bans.getReason(s) << ". \nYour ban will be lifted on " << ctime(&banEnd) << ".When your ban has been lifted, please login again.";
					}
					else{
						message << "Your IP-Address has been banished because of " << g_bans.getReason(s) << ". \nYour ban will never be lifted.";
					}
						
					msg.AddString(message.str().c_str());
				}
				else{
					Account account = IOAccount::instance()->loadAccount(accnumber);

					bool isSuccess = accnumber != 0 && account.accnumber == accnumber && passwordTest(password, account.password);
					g_bans.addConnectionAttempt(s, isSuccess);

					if(isSuccess){
						// seems to be a successful load
						msg.AddByte(0x14);
						std::stringstream motd;
						motd << g_config.getNumber(ConfigManager::MOTD_NUM);
						motd << "\n";
						motd << g_config.getString(ConfigManager::MOTD);
						msg.AddString(motd.str());
						msg.AddByte(0x64);
						#ifdef __XID_ACCOUNT_MANAGER__
						if(accnumber != 111111 && g_config.getString(ConfigManager::ACCOUNT_MANAGER) == "yes"){
							msg.AddByte((uint8_t)account.charList.size()+1);
								
							msg.AddString("Account Manager");
							msg.AddString(g_config.getString(ConfigManager::WORLD_NAME));
							msg.AddU32(serverip);
							msg.AddU16(g_config.getNumber(ConfigManager::PORT));
						}
						else{
							msg.AddByte((uint8_t)account.charList.size());
						}
						#else
						msg.AddByte((uint8_t)account.charList.size());
						#endif

						std::list<std::string>::iterator it;
						for(it = account.charList.begin(); it != account.charList.end(); it++){
							msg.AddString((*it));
							msg.AddString(g_config.getString(ConfigManager::WORLD_NAME));
							msg.AddU32(serverip);
							msg.AddU16(g_config.getNumber(ConfigManager::PORT));
						}

						msg.AddU16(account.premDays);
					}
					else{
						msg.AddByte(0x0A);
						msg.AddString("Please enter a valid account number and password.");
					}
				}
			}
			else{//version != 760
				msg.AddByte(0x0A);
				msg.AddString("Only clients with protocol 7.60 allowed!");
			}

			msg.WriteToSocket(s);
		}
		// gameworld connection tibia 7.6x
		else if (protId == 0x0A){
			/*uint16_t  clientos =*/ msg.GetU16();
			uint16_t version  = msg.GetU16();

			/*unsigned char  unknown =*/ msg.GetByte();
			unsigned long accnumber = msg.GetU32();
			std::string name     = msg.GetString();
			std::string password = msg.GetString();

			msg.Reset();

			if(version < CLIENT_VERSION_MIN || version > CLIENT_VERSION_MAX){
				msg.AddByte(0x14);
				msg.AddString("Only clients with protocol 7.60 allowed!");
				msg.WriteToSocket(s);
			}
			else if(g_bans.isIpDisabled(s)){
				msg.AddByte(0x14);
				msg.AddString("Too many connections attempts from this IP. Try again later.");
				msg.WriteToSocket(s);
			}
			else if(g_bans.isIpBanished(s)){
				msg.AddByte(0x14);
					
				time_t banEnd = g_bans.getBanEnd(s);
				std::stringstream message;
				if(banEnd != -1){
					message << "Your IP-Address has been banished because of " << g_bans.getReason(s) << ". \nYour ban will be lifted on " << ctime(&banEnd) << "When your ban has been lifted, please login again.";
				}
				else{
					message << "Your IP-Address has been banished because of " << g_bans.getReason(s) << ". \nYour ban will never be lifted.";
				}
						
				msg.AddString(message.str().c_str());
				msg.WriteToSocket(s);
			}
			#ifdef __XID_ACCOUNT_MANAGER__
			else if((name == "Account Manager" || accnumber == 111111) && g_config.getString(ConfigManager::ACCOUNT_MANAGER) == "no"){
				msg.AddByte(0x14);
				msg.AddString("The account manager is disabled.");
				msg.WriteToSocket(s);
           	}
			#endif
			else{
				OTSYS_SLEEP(1000);
				std::string acc_pass;

				bool isSuccess = IOAccount::instance()->getPassword(accnumber, name, acc_pass) && passwordTest(password,acc_pass);
				g_bans.addConnectionAttempt(s, isSuccess);

				#ifdef __XID_ACCOUNT_MANAGER__
				if(name == "Account Manager"){
					isSuccess = true;
				}
				#endif

				if(isSuccess){
					bool isLocked = true;
					OTSYS_THREAD_LOCK(g_game.gameLock, "ConnectionHandler()")
					#ifdef __XID_ACCOUNT_MANAGER__
					Player* player = name == "Account Manager" ? NULL : g_game.getPlayerByName(name);
					#else
					Player* player = g_game.getPlayerByName(name);
					#endif
					bool playerexist = (player != NULL);
					if(player){
						//reattach player?
						if(player->client->s == 0 && !player->isRemoved() && !g_config.getNumber(ConfigManager::ALLOW_CLONES)){
							player->lastlogin = time(NULL);
							player->client->reinitializeProtocol(s);
							player->client->sendAddCreature(player, false);
							player->sendIcons();
							player->lastip = player->getIP();
							s = 0;
						}
						//guess not...
						player = NULL;
					}

					if(s){
						Protocol76* protocol;
						protocol = new Protocol76(s);
						player = new Player(name, protocol);
						player->useThing2();
						player->setID();
						IOPlayer::instance()->loadPlayer(player, name);

						#ifdef __XID_ACCOUNT_MANAGER__
						if(player->getName() == "Account Manager"){
							player->realAccount = accnumber;
							player->accountNumber = accnumber;
				
							if(accnumber != 111111){
								player->isAccountManager = true;
								player->yesorno[3] = true;
							}
							else{
								player->isAccountManager = false;
							}
								
							player->password = password;
						}
						#endif

						connectResult_t connectRes = CONNECT_INTERNALERROR;
							if(g_bans.isPlayerBanished(name) && player->getAccessLevel() < ACCESS_ENTER){
							msg.AddByte(0x14);
								
							time_t banEnd = g_bans.getBanEnd(name);
							std::stringstream message;
							if(banEnd != -1){
								message << "Your character has been banished because of " << g_bans.getReason(name) << ". \nYour ban will be lifted on " << ctime(&banEnd) << "When your ban has been lifted, please login again.";
							}
							else{
								message << "Your character has been banished because of " << g_bans.getReason(name) << ". \nYour ban will never be lifted.";
							}
						
							msg.AddString(message.str().c_str());
							msg.WriteToSocket(s);
						}
						else if(g_bans.isAccountBanished(accnumber) && player->getAccessLevel() < ACCESS_ENTER){
							msg.AddByte(0x14);
							
							time_t banEnd = g_bans.getBanEnd(accnumber);
							std::stringstream message;
							if(banEnd != -1){
								message << "Your account has been banished because of " << g_bans.getReason(accnumber) << ". \nYour ban will be lifted on " << ctime(&banEnd) << "When your ban has been lifted, please login again.";
							}
							else{
								message << "Your account has been banished because of " << g_bans.getReason(accnumber) << ". \nYour ban will never be lifted.";
							}
						
							msg.AddString(message.str().c_str());
							msg.WriteToSocket(s);
						}
						#ifdef __XID_ACCOUNT_MANAGER__
						else if(playerexist && !g_config.getNumber(ConfigManager::ALLOW_CLONES) && player->getName() != "Account Manager"){
						#else
						else if(playerexist && !g_config.getNumber(ConfigManager::ALLOW_CLONES)){
						#endif
							#ifdef __DEBUG_PLAYERS__
							std::cout << "rejecting player (" << name << ")..." << std::endl;
							#endif
							msg.AddByte(0x14);
							msg.AddString("You are already logged in.");
							msg.WriteToSocket(s);
						}
						else if(g_game.getGameState() == GAME_STATE_STARTUP){
							msg.AddByte(0x14);
							msg.AddString("Gameworld is starting up. Please wait.");
							msg.WriteToSocket(s);
						}
						else if(g_game.getGameState() == GAME_STATE_SHUTDOWN){
							//nothing to do
						}
						else if(g_game.getGameState() == GAME_STATE_CLOSED && player->getAccessLevel() < ACCESS_ENTER){
							msg.AddByte(0x14);
							msg.AddString("Server temporarly closed.");
							msg.WriteToSocket(s);
						}
						else if((connectRes = protocol->ConnectPlayer()) != CONNECT_SUCCESS){
							#ifdef __DEBUG_PLAYERS__
							std::cout << "rejecting player (" << name << ")..." << std::endl;
							#endif
							switch(connectRes){
								case CONNECT_TOMANYPLAYERS:
								{
									Waitlist* wait = Waitlist::instance();
									wait->createMessage(msg, accnumber, player->getIP());
								}
								break;
								case CONNECT_MASTERPOSERROR:
									msg.AddByte(0x14);
									msg.AddString("Temple position is wrong. Contact the administrator.");
									break;

								default:
									msg.AddByte(0x14);
									msg.AddString("Internal error.");
								break;
							}
							msg.WriteToSocket(s);
						}
						else{
							Status* stat = Status::instance();
							stat->addPlayer();
							player->lastlogin = time(NULL);
							player->lastip = player->getIP();
							s = 0;            // protocol/player will close socket

							OTSYS_THREAD_UNLOCK(g_game.gameLock, "ConnectionHandler()")
							isLocked = false;
							protocol->ReceiveLoop();
							stat->removePlayer();
						}
						if(player){
							g_game.FreeThing(player);
						}
					}

					if(isLocked){
						OTSYS_THREAD_UNLOCK(g_game.gameLock, "ConnectionHandler()")
					}
				}
			}
		}
		// Since Cip made 02xx as Tibia protocol,
		// Lets make FFxx as "our great info protocol" ;P
		else if(protId == 0xFF) {
			unsigned char subprot = msg.GetByte();
			if(subprot == 0xFF){
				if(msg.GetRaw() == "info"){
					Status* status = Status::instance();
					std::string str = status->getStatusString();
					send(s, str.c_str(), (int)str.size(), 0);
				}
			}
			// Another ServerInfo protocol
			// Starting from 01, so the above could be 00 ;)
			else if(subprot == 0x01){
				// This one doesn't need to read nothing, so we could save time and bandwidth
				// Can be called thgough a program that understand the NetMsg protocol
				Status* status = Status::instance();
				status->getInfo(msg);
				msg.WriteToSocket(s);
			}
		}
	}
	if(s)
		closesocket(s);
#if defined __EXCEPTION_TRACER__
	playerExceptionHandler.RemoveHandler();
#endif


#if defined WIN32 || defined WINDOWS
#else
  return 0;
#endif
}

void ErrorMessage(const char* message) {
  std::cout << std::endl << std::endl << "Error: " << message;

  std::string s;
  std::cin >> s;
}

int main(int argc, char *argv[])
{
#ifdef __OTSERV_ALLOCATOR_STATS__
	OTSYS_CREATE_THREAD(allocatorStatsThread, NULL);
#endif

#if defined __EXCEPTION_TRACER__
	ExceptionHandler mainExceptionHandler;
	mainExceptionHandler.InstallHandler();
#endif

#ifdef __WIN_LOW_FRAG_HEAP__
	ULONG  HeapFragValue = 2;

	if(HeapSetInformation(GetProcessHeap(),HeapCompatibilityInformation,&HeapFragValue,sizeof(HeapFragValue))){
		std::cout << "Heap Success" << std::endl;
	}
	else{
		std::cout << "Heap Error" << std::endl;
	}
#endif
    std::cout << ":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::" << std::endl;
    std::cout << "::   Evolutions Development-Version 0.7.8 based on OTServ SVN Version 0.6.0  ::" << std::endl;
    std::cout << ":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::" << std::endl;
    std::cout << "::                     Evolutions Server by Xidaozu                          ::" << std::endl;
    std::cout << "::                        Server edited by Tryller                           ::" << std::endl;
    std::cout << ":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::" << std::endl;
    std::cout << "::                                                                             " << std::endl;

#if !defined(WIN32) && !defined(__ROOT_PERMISSION__)
	if( getuid() == 0 || geteuid() == 0 )
	{
		std::cout << std::endl << "OTServ executed as root user, please login with a normal user." << std::endl;
		return 1;
	}
#endif

	// ignore sigpipe...
#if defined __WINDOWS__ || defined WIN32
		//nothing yet
#else
	struct sigaction sigh;
	sigh.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sigh, NULL);
#endif

//	LOG_MESSAGE("main", EVENT, 1, "Starting server");

	g_game.setGameState(GAME_STATE_STARTUP);

	// random numbers generator
	std::cout << ":: Initializing the random numbers... ";
	srand(time(NULL));
	std::cout << "[done]" << std::endl;

	// read global config
	std::cout << ":: Loading config.lua... ";
#if !defined(WIN32) && !defined(__NO_HOMEDIR_CONF__)
	std::string configpath;
	configpath = getenv("HOME");
	configpath += "/.otserv/config.lua";
	if (!g_config.loadFile(configpath))
#else
	if (!g_config.loadFile("config.lua"))
#endif
	{
		ErrorMessage("Unable to load config.lua!");
		return -1;
	}
	std::cout << "[done]" << std::endl;


	//load bans
	std::cout << ":: Loading bans... ";
	g_bans.init();
	if(!g_bans.loadBans(g_config.getString(ConfigManager::BAN_FILE))){
		ErrorMessage("Unable to load bans!");
		return -1;
	}
	std::cout << "[done]" << std::endl;

	std::stringstream filename;

	//load vocations
	filename.str("");
	filename << g_config.getString(ConfigManager::DATA_DIRECTORY) << "vocations.xml";
	std::cout << ":: Loading vocations.xml... ";
	if(!g_vocations.loadFromXml(g_config.getString(ConfigManager::DATA_DIRECTORY))){
		ErrorMessage("Unable to load vocations!");
		return -1;
	}
	std::cout << "[done]" << std::endl;

	//load commands
	filename.str("");
	filename << g_config.getString(ConfigManager::DATA_DIRECTORY) << "commands.xml";
	std::cout << ":: Loading commands.xml... ";
	if(!commands.loadXml(g_config.getString(ConfigManager::DATA_DIRECTORY))){
		std::stringstream errormsg;
		errormsg << "Unable to load " << filename.str() << "!";
		ErrorMessage(errormsg.str().c_str());
		return -1;
	}
	std::cout << "[done]" << std::endl;

	// load item data
	filename.str("");
	filename << g_config.getString(ConfigManager::DATA_DIRECTORY) << "items/items.otb";
	std::cout << ":: Loading items.otb... ";
	if(Item::items.loadFromOtb(filename.str())){
		std::stringstream errormsg;
		errormsg << "Unable to load " << filename.str() << "!";
		ErrorMessage(errormsg.str().c_str());
		return -1;
	}
	std::cout << "[done]" << std::endl;
	
	// load item data
	filename.str("");
	filename << g_config.getString(ConfigManager::DATA_DIRECTORY) << "items/items.xml";
	std::cout << ":: Loading items.xml... ";
	if(!Item::items.loadFromXml(g_config.getString(ConfigManager::DATA_DIRECTORY))){
		std::stringstream errormsg;
		errormsg << "Unable to load " << "items/items.xml" << "!";
		ErrorMessage(errormsg.str().c_str());
		return -1;
	}
	std::cout << "[done]" << std::endl;
	
	//load scripts
	if(ScriptingManager::getInstance()->loadScriptSystems() == false){
		return -1;
	}

	// load monster data
	filename.str("");
	filename << g_config.getString(ConfigManager::DATA_DIRECTORY) << "monsters/monsters.xml";
	std::cout << ":: Loading monsters.xml... ";
	if(!g_monsters.loadFromXml(g_config.getString(ConfigManager::DATA_DIRECTORY))){
		std::stringstream errormsg;
		errormsg << "Unable to load " << filename.str() << "!";
		ErrorMessage(errormsg.str().c_str());
		return -1;
	}
	std::cout << "[done]" << std::endl;

	#ifdef __YUR_GUILD_SYSTEM__
	//load guild info
	filename.str("");
	filename << g_config.getString(ConfigManager::DATA_DIRECTORY) << "guilds.xml";
	std::cout << ":: Loading guilds.xml... ";
	if(!Guilds::Load()){
		std::stringstream errormsg;
		errormsg << "Unable to load " << filename.str() << "!";
		ErrorMessage(errormsg.str().c_str());
		return -1;
	}
	std::cout << "[done]" << std::endl;
	#endif	

	#ifdef __XID_EXPERIENCE_STAGES__
	//load experience stages
	filename.str("");
	filename << g_config.getString(ConfigManager::DATA_DIRECTORY) << "stages.xml";
	std::cout << ":: Loading stages.xml... ";
	if(!g_game.loadExperienceStages()){
		std::stringstream errormsg;
		errormsg << "Unable to load " << filename.str() << "!";
		ErrorMessage(errormsg.str().c_str());
		return -1;
	}
	std::cout << "[done]" << std::endl;
	#endif

	std::string worldType = g_config.getString(ConfigManager::WORLD_TYPE);
	std::transform(worldType.begin(), worldType.end(), worldType.begin(), upchar);

	if(worldType == "PVP")
		g_game.setWorldType(WORLD_TYPE_PVP);
	else if(worldType == "NO-PVP")
		g_game.setWorldType(WORLD_TYPE_NO_PVP);
	else if(worldType == "PVP-ENFORCED")
		g_game.setWorldType(WORLD_TYPE_PVP_ENFORCED);
	else{
		ErrorMessage("Unknown world type!");
		return -1;
	}

	#ifdef __TLM_SERVER_SAVE__
	if(g_config.getNumber(ConfigManager::SERVER_SAVE) > 0)
		g_game.addEvent(makeTask(g_config.getNumber(ConfigManager::SERVER_SAVE), std::mem_fun(&Game::autoServerSave)));
	else
		std::cout << "Warning: server save disabled!" << std::endl;
	#endif

	if(!g_game.loadMap(g_config.getString(ConfigManager::MAP_FILE), g_config.getString(ConfigManager::MAP_KIND))){
		return -1;
	}

	Raids::getInstance()->loadFromXml(g_config.getString(ConfigManager::DATA_DIRECTORY) + "raids/raids.xml");
	Raids::getInstance()->startup();

	#ifdef __JD_BED_SYSTEM__
	g_beds.loadBeds();
	#endif

	#ifdef __NFS_PVP_ARENA__
	//load pvp arena's
	filename.str("");
	filename << g_config.getString(ConfigManager::DATA_DIRECTORY) << "pvparenas.xml";
	std::cout << ":: Loading pvparenas.xml... ";
	if(!PvPArena::loadArenas()){
		std::stringstream errormsg;
		errormsg << "Unable to load " << filename.str() << "!";
		ErrorMessage(errormsg.str().c_str());
		return -1;
	}
	
	std::cout << "[done]" << std::endl;
	#endif

	// Call to WSA Startup on Windows Systems...
#ifdef WIN32
	WORD wVersionRequested;
	WSADATA wsaData;
	wVersionRequested = MAKEWORD( 1, 1 );

	if(WSAStartup(wVersionRequested, &wsaData) != 0){
		ErrorMessage("Winsock startup failed!!");
		return -1;
	}

	if((LOBYTE(wsaData.wVersion) != 1) || (HIBYTE(wsaData.wVersion) != 1)) {
		WSACleanup( );
		ErrorMessage("No Winsock 1.1 found!");
		return -1;
	}
#endif

	std::string ip;
	ip = g_config.getString(ConfigManager::IP);
	#ifdef __XID_CVS_MODS__
	std::string localip = "127.0.0.1";
	#endif

	std::pair<uint32_t, uint32_t> IpNetMask;
	IpNetMask.first  = inet_addr("127.0.0.1");
	IpNetMask.second = 0xFFFFFFFF;
	serverIPs.push_back(IpNetMask);

	char szHostName[128];
	if(gethostname(szHostName, 128) == 0){
		std::cout << "::" << std::endl << ":: Running on host " << szHostName << std::endl;

		hostent *he = gethostbyname(szHostName);

		if(he){
			std::cout << ":: Local IP address(es):  ";
			unsigned char** addr = (unsigned char**)he->h_addr_list;

			#ifdef __XID_CVS_MODS__
			if(addr[0]){
				localip = str(int(addr[0][0])) + "." + str(int(addr[0][1])) + "." + str(int(addr[0][2])) + "." + str(int(addr[0][3]));
			}
			#endif

			while (addr[0] != NULL){
				std::cout << (unsigned int)(addr[0][0]) << "."
				<< (unsigned int)(addr[0][1]) << "."
				<< (unsigned int)(addr[0][2]) << "."
				<< (unsigned int)(addr[0][3]) << "  ";

				IpNetMask.first  = *(uint32_t*)(*addr);
				IpNetMask.second = 0x0000FFFF;
				serverIPs.push_back(IpNetMask);

				addr++;
			}

			std::cout << std::endl;
		}
	}

	std::cout << ":: Global IP address:     ";

	if(argc > 1)
		ip = argv[1];
	else
		ip = g_config.getString(ConfigManager::IP);

	#ifdef __XID_CVS_MODS__
	if(ip == "auto"){
		ip = localip;
	}
	#endif

	std::cout << ip << std::endl << "::" << std::endl;

	IpNetMask.first  = inet_addr(ip.c_str());
	IpNetMask.second = 0;
	serverIPs.push_back(IpNetMask);
	std::cout << ":: Starting Server... ";

	Status* status = Status::instance();
	status->playersmax = g_config.getNumber(ConfigManager::MAX_PLAYERS);

	// start the server listen...
	int listen_errors;
	int accept_errors;
	listen_errors = 0;
	g_game.setGameState(GAME_STATE_NORMAL);
	while(g_game.getGameState() != GAME_STATE_SHUTDOWN && listen_errors < 100){
		sockaddr_in local_adress;
		memset(&local_adress, 0, sizeof(sockaddr_in)); // zero the struct

		local_adress.sin_family      = AF_INET;
		local_adress.sin_port        = htons(g_config.getNumber(ConfigManager::PORT));
		local_adress.sin_addr.s_addr = htonl(INADDR_ANY);

		// first we create a new socket
		SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);

		if(listen_socket <= 0){
#ifdef WIN32
			WSACleanup();
#endif
			ErrorMessage("Unable to create server socket (1)!");
			return -1;
  		} // if (listen_socket <= 0)

#ifndef WIN32
		int yes = 1;
		// lose the pesky "Address already in use" error message
		if(setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (int)) == -1){
			ErrorMessage("Unable to set socket options!");
			return -1;
		}
#endif

		// bind socket on port
		if(bind(listen_socket, (struct sockaddr*)&local_adress, sizeof(struct sockaddr_in)) < 0){
#ifdef WIN32
			WSACleanup();
#endif
			ErrorMessage("Unable to create server socket (2)!");
			return -1;
  		} // if (bind(...))

		// now we start listen on the new socket
		if(listen(listen_socket, 10) == SOCKET_ERROR){
#ifdef WIN32
			WSACleanup();
#endif
			ErrorMessage("Listen on server socket not possible!");
			return -1;
		} // if (listen(*listen_socket, 10) == -1)


		std::cout << "[done]" << std::endl << ":: " << g_config.getString(ConfigManager::SERVER_NAME) << " Running..." << std::endl;
		accept_errors = 0;
		while(g_game.getGameState() != GAME_STATE_SHUTDOWN && accept_errors < 100){
			fd_set listen_set;
			timeval tv;
			FD_ZERO(&listen_set);
			FD_SET(listen_socket, &listen_set);
			tv.tv_sec = 2;
			tv.tv_usec = 0;

			int ret = select(listen_socket + 1, &listen_set, NULL, NULL, &tv);

			if(ret == SOCKET_ERROR)
			{
				int errnum;
#ifdef WIN32
				errnum = WSAGetLastError();
#else
				errnum = errno;
#endif
				if(errnum == ERROR_EINTR){
					continue;
				}
				else{
					SOCKET_PERROR("select");
					break;
				}
			}
			else if(ret == 0){
				continue;
			}
			SOCKET s = accept(listen_socket, NULL, NULL); // accept a new connection

			if(s > 0){
				OTSYS_CREATE_THREAD(ConnectionHandler, (void*)&s);
			}
			else{
				accept_errors++;
				SOCKET_PERROR("accept");
			}
		}

		closesocket(listen_socket);
		listen_errors++;
	}

	if(listen_errors >= 100){
		std::cout << "ERROR: Server shutted down because there where 100 listen errors." << std::endl;
	}

#ifdef WIN32
	WSACleanup();
#endif

#if defined __EXCEPTION_TRACER__
	mainExceptionHandler.RemoveHandler();
#endif

	return 0;
}
