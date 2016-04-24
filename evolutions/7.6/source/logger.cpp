//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
// Logger class - captures everything that happens on the server
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

#include "logger.h"
#include <iostream>

Logger::Logger()
{
	m_file = fopen("data/logs/remote-control.txt", "a");
}

Logger::~Logger()
{
	if(m_file){
		fclose(m_file);
	}
}

void Logger::logMessage(char* channel, eLogType type, int level, std::string message, const char* func)
{
	//TODO: decide if should be saved or not depending on channel type and level
	// if should be save decide where and how
	
	//write timestamp of the event
	time_t tmp = time(NULL);
	const tm* tms = localtime(&tmp);
	if(tms){
		fprintf(m_file, "%02d/%02d/%04d  %02d:%02d:%02d", tms->tm_mday, tms->tm_mon + 1, tms->tm_year + 1900,
			tms->tm_hour, tms->tm_min, tms->tm_sec);
	}
	//write channel generating the message
	if(channel){
		fprintf(m_file, " [%s] ", channel);
	}
	//write message type
	char* type_str;
	switch(type){
	case LOGTYPE_EVENT:
		type_str = "event";
		break;
	case LOGTYPE_WARNING:
		type_str = "warning";
		break;
	case LOGTYPE_ERROR:
		type_str = "ERROR";
		break;
	default:
		type_str = "???";
		break;
	}
	fprintf(m_file, " %s:", type_str);
	//write the message
	fprintf(m_file, " %s\n", message.c_str());

	fflush(m_file);
}
