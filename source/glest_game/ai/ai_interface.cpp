//
//	ai_interface.cpp:
//
//	This file is part of ZetaGlest <https://github.com/ZetaGlest>
//
//	Copyright (C) 2018  The ZetaGlest team
//
//	ZetaGlest is a fork of MegaGlest <https://megaglest.org>
//
//	This program is free software: you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.

//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program.  If not, see <https://www.gnu.org/licenses/>

#include "ai_interface.h"

#include "ai.h"
#include "command_type.h"
#include "faction.h"
#include "unit.h"
#include "unit_type.h"
#include "object.h"
#include "game.h"
#include "config.h"
#include "network_manager.h"
#include "platform_util.h"
#include "leak_dumper.h"

using namespace Shared::Util;
using namespace Shared::Graphics;

// =====================================================
// 	class AiInterface
// =====================================================

namespace Glest{ namespace Game{

// =====================================================
//	class FactionThread
// =====================================================

AiInterfaceThread::AiInterfaceThread(AiInterface *aiIntf) : BaseThread() {
	this->masterController = NULL;
	this->triggerIdMutex = new Mutex(CODE_AT_LINE);
	this->aiIntf = aiIntf;
	uniqueID = "AiInterfaceThread";
}

AiInterfaceThread::~AiInterfaceThread() {
	delete triggerIdMutex;
	triggerIdMutex = NULL;
}

void AiInterfaceThread::setQuitStatus(bool value) {
	if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s] Line: %d value = %d\n",__FILE__,__FUNCTION__,__LINE__,value);

	BaseThread::setQuitStatus(value);
	if(value == true) {
		signal(-1);
	}

	if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s] Line: %d\n",__FILE__,__FUNCTION__,__LINE__);
}

void AiInterfaceThread::signal(int frameIndex) {
	if(frameIndex >= 0) {
		static string mutexOwnerId = string(__FILE__) + string("_") + intToStr(__LINE__);
		MutexSafeWrapper safeMutex(triggerIdMutex,mutexOwnerId);
		this->frameIndex.first = frameIndex;
		this->frameIndex.second = false;

		safeMutex.ReleaseLock();
	}
	semTaskSignalled.signal();
}

void AiInterfaceThread::setTaskCompleted(int frameIndex) {
	if(frameIndex >= 0) {
		static string mutexOwnerId = string(__FILE__) + string("_") + intToStr(__LINE__);
		MutexSafeWrapper safeMutex(triggerIdMutex,mutexOwnerId);
		if(this->frameIndex.first == frameIndex) {
			this->frameIndex.second = true;
		}
		safeMutex.ReleaseLock();
	}
}

bool AiInterfaceThread::canShutdown(bool deleteSelfIfShutdownDelayed) {
	bool ret = (getExecutingTask() == false);
	if(ret == false && deleteSelfIfShutdownDelayed == true) {
	    setDeleteSelfOnExecutionDone(deleteSelfIfShutdownDelayed);
	    deleteSelfIfRequired();
	    signalQuit();
	}

	return ret;
}

bool AiInterfaceThread::isSignalCompleted(int frameIndex) {
	if(getRunningStatus() == false) {
		return true;
	}
	static string mutexOwnerId = string(__FILE__) + string("_") + intToStr(__LINE__);
	MutexSafeWrapper safeMutex(triggerIdMutex,mutexOwnerId);
	//bool result = (event != NULL ? event->eventCompleted : true);
	bool result = (this->frameIndex.first == frameIndex && this->frameIndex.second == true);

	//if(SystemFlags::VERBOSE_MODE_ENABLED) printf("In [%s::%s Line: %d] worker thread this = %p, this->frameIndex.first = %d, this->frameIndex.second = %d\n",__FILE__,__FUNCTION__,__LINE__,this,this->frameIndex.first,this->frameIndex.second);

	safeMutex.ReleaseLock();
	return result;
}

void AiInterfaceThread::signalQuit() {
	if(this->aiIntf != NULL) {
		MutexSafeWrapper safeMutex(this->aiIntf->getMutex(),string(__FILE__) + "_" + intToStr(__LINE__));
		this->aiIntf = NULL;
	}

	BaseThread::signalQuit();
}
void AiInterfaceThread::execute() {
    RunningStatusSafeWrapper runningStatus(this);
	try {
		//setRunningStatus(true);
		if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);
		if(SystemFlags::VERBOSE_MODE_ENABLED) printf("In [%s::%s Line: %d] ****************** STARTING worker thread this = %p\n",__FILE__,__FUNCTION__,__LINE__,this);

		//bool minorDebugPerformance = false;
		Chrono chrono;

		//unsigned int idx = 0;
		for(;this->aiIntf != NULL;) {
			if(getQuitStatus() == true) {
				if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);
				break;
			}

			semTaskSignalled.waitTillSignalled();

			static string masterSlaveOwnerId = string(__FILE__) + string("_") + intToStr(__LINE__);
			MasterSlaveThreadControllerSafeWrapper safeMasterController(masterController,20000,masterSlaveOwnerId);

			if(getQuitStatus() == true) {
				if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);
				break;
			}

			static string mutexOwnerId = string(__FILE__) + string("_") + intToStr(__LINE__);
            MutexSafeWrapper safeMutex(triggerIdMutex,mutexOwnerId);
            bool executeTask = (frameIndex.first >= 0);

            //if(SystemFlags::VERBOSE_MODE_ENABLED) printf("In [%s::%s Line: %d] frameIndex = %d this = %p executeTask = %d\n",__FILE__,__FUNCTION__,__LINE__,frameIndex.first, this, executeTask);

            safeMutex.ReleaseLock();

            if(executeTask == true) {
				ExecutingTaskSafeWrapper safeExecutingTaskMutex(this);

				MutexSafeWrapper safeMutex(this->aiIntf->getMutex(),string(__FILE__) + "_" + intToStr(__LINE__));

				this->aiIntf->update();

				safeMutex.ReleaseLock();

				setTaskCompleted(frameIndex.first);
            }

			if(getQuitStatus() == true) {
				if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);
				break;
			}
		}

		if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);
		if(SystemFlags::VERBOSE_MODE_ENABLED) printf("In [%s::%s Line: %d] ****************** ENDING worker thread this = %p\n",__FILE__,__FUNCTION__,__LINE__,this);
	}
	catch(const exception &ex) {
		//setRunningStatus(false);

		SystemFlags::OutputDebug(SystemFlags::debugError,"In [%s::%s Line: %d] Error [%s]\n",__FILE__,__FUNCTION__,__LINE__,ex.what());
		if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);

		throw megaglest_runtime_error(ex.what());
	}
	if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s] Line: %d\n",__FILE__,__FUNCTION__,__LINE__);
}

AiInterface::AiInterface(Game &game, int factionIndex, int teamIndex,
		int useStartLocation) : fp(NULL) {
	if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);

	this->aiMutex = new Mutex(CODE_AT_LINE);
	this->workerThread = NULL;
	this->world= game.getWorld();
	this->commander= game.getCommander();
	this->console= game.getConsole();
	this->gameSettings = game.getGameSettings();

	this->factionIndex= factionIndex;
	this->teamIndex= teamIndex;
	timer= 0;

	//init ai
	ai.init(this,useStartLocation);

	//config
	logLevel= Config::getInstance().getInt("AiLog");
	redir= Config::getInstance().getBool("AiRedir");

	aiLogFile = getLogFilename();
	if(getGameReadWritePath(GameConstants::path_logs_CacheLookupKey) != "") {
		aiLogFile = getGameReadWritePath(GameConstants::path_logs_CacheLookupKey) + aiLogFile;
	}
	else {
        string userData = Config::getInstance().getString("UserData_Root","");
        if(userData != "") {
        	endPathWithSlash(userData);
        }
        aiLogFile = userData + aiLogFile;
	}

	//clear log file
	if(logLevel > 0) {
#ifdef WIN32
		fp = _wfopen(::Shared::Platform::utf8_decode(aiLogFile).c_str(), L"wt");
#else
		fp = fopen(aiLogFile.c_str(), "wt");
#endif
		if(fp == NULL) {
			throw megaglest_runtime_error("Can't open file: [" + aiLogFile + "]");
		}
		fprintf(fp, "MegaGlest AI log file for Tech [%s] Faction [%s] #%d\n\n",this->gameSettings->getTech().c_str(),this->world->getFaction(this->factionIndex)->getType()->getName().c_str(),this->factionIndex);
	}


	if( Config::getInstance().getBool("EnableAIWorkerThreads","true") == true) {
		if(workerThread != NULL) {
			workerThread->signalQuit();
			if(workerThread->shutdownAndWait() == true) {
				delete workerThread;
			}
			workerThread = NULL;
		}
		static string mutexOwnerId = string(extractFileFromDirectoryPath(__FILE__).c_str()) + string("_") + intToStr(__LINE__);
		this->workerThread = new AiInterfaceThread(this);
		this->workerThread->setUniqueID(mutexOwnerId);
		this->workerThread->start();
	}

	if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);
}

void AiInterface::init() {
    world=NULL;;
    commander=NULL;;
    console=NULL;;
    gameSettings=NULL;;
    timer=0;
    factionIndex=0;
    teamIndex=0;
	redir=false;
    logLevel=0;
    fp=NULL;;
    aiMutex=NULL;
    workerThread=NULL;
}

AiInterface::~AiInterface() {
	if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d] deleting AI factionIndex = %d, teamIndex = %d\n",__FILE__,__FUNCTION__,__LINE__,this->factionIndex,this->teamIndex);
    cacheUnitHarvestResourceLookup.clear();

	if(workerThread != NULL) {
		workerThread->signalQuit();
    	sleep(0);
    	if(workerThread->canShutdown(true) == true &&
    			workerThread->shutdownAndWait() == true) {
			delete workerThread;
		}
		workerThread = NULL;
	}

    if(fp) {
    	fclose(fp);
    	fp = NULL;
    }

	delete aiMutex;
	aiMutex = NULL;
}

void AiInterface::signalWorkerThread(int frameIndex) {
	if(workerThread != NULL) {
		workerThread->signal(frameIndex);
	}
	else {
		this->update();
	}
}

bool AiInterface::isWorkerThreadSignalCompleted(int frameIndex) {
	if(workerThread != NULL) {
		return workerThread->isSignalCompleted(frameIndex);
	}
	return true;
}

// ==================== main ====================

void AiInterface::update() {
	timer++;
	ai.update();
}

// ==================== misc ====================

bool AiInterface::isLogLevelEnabled(int level) {
	return (this->logLevel >= level);
}

void AiInterface::printLog(int logLevel, const string &s){
    if(isLogLevelEnabled(logLevel) == true) {
		string logString= "(" + intToStr(factionIndex) + ") " + s;

		MutexSafeWrapper safeMutex(aiMutex,string(__FILE__) + "_" + intToStr(__LINE__));
		//print log to file
		if(fp != NULL) {
			fprintf(fp, "%s\n", logString.c_str());
		}

		//redirect to console
		if(redir) {
			console->addLine(logString);
		}
    }
}

// ==================== interaction ====================

Faction *AiInterface::getMyFaction() {
	return world->getFaction(factionIndex);
}

bool AiInterface::executeCommandOverNetwork() {
	bool enableServerControlledAI 	= gameSettings->getEnableServerControlledAI();
	bool isNetworkGame 				= gameSettings->isNetworkGame();
	NetworkRole role 				= NetworkManager::getInstance().getNetworkRole();
	Faction *faction 				= world->getFaction(factionIndex);
	return faction->getCpuControl(enableServerControlledAI,isNetworkGame,role);
}

std::pair<CommandResult,string> AiInterface::giveCommandSwitchTeamVote(const Faction* faction, SwitchTeamVote *vote) {
	assert(this->gameSettings != NULL);

	commander->trySwitchTeamVote(faction,vote);
	return std::pair<CommandResult,string>(crSuccess,"");
}

std::pair<CommandResult,string> AiInterface::giveCommand(int unitIndex, CommandClass commandClass, const Vec2i &pos){
	assert(this->gameSettings != NULL);

	std::pair<CommandResult,string> result(crFailUndefined,"");
	if(executeCommandOverNetwork() == true) {
		const Unit *unit = getMyUnit(unitIndex);
		result = commander->tryGiveCommand(unit, unit->getType()->getFirstCtOfClass(commandClass), pos, unit->getType(),CardinalDir(CardinalDir::NORTH));
		return result;
	}
	else {
		Command *c= new Command (world->getFaction(factionIndex)->getUnit(unitIndex)->getType()->getFirstCtOfClass(commandClass), pos);

		if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);
		result = world->getFaction(factionIndex)->getUnit(unitIndex)->giveCommand(c);
		if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);

		return result;
	}
}

std::pair<CommandResult,string> AiInterface::giveCommand(const Unit *unit, const CommandType *commandType, const Vec2i &pos, int unitGroupCommandId) {
	assert(this->gameSettings != NULL);

	std::pair<CommandResult,string> result(crFailUndefined,"");
	if(unit == NULL) {
	    char szBuf[8096]="";
	    snprintf(szBuf,8096,"In [%s::%s Line: %d] Can not find AI unit in AI factionIndex = %d. Game out of synch.",__FILE__,__FUNCTION__,__LINE__,factionIndex);
		throw megaglest_runtime_error(szBuf);
	}
    const UnitType* unitType= unit->getType();
	if(unitType == NULL) {
	    char szBuf[8096]="";
	    snprintf(szBuf,8096,"In [%s::%s Line: %d] Can not find AI unittype with unit id: %d, AI factionIndex = %d. Game out of synch.",__FILE__,__FUNCTION__,__LINE__,unit->getId(),factionIndex);
		throw megaglest_runtime_error(szBuf);
	}
	if(commandType == NULL) {
	    char szBuf[8096]="";
	    snprintf(szBuf,8096,"In [%s::%s Line: %d] commandType == NULL, unit id: %d, AI factionIndex = %d. Game out of synch.",__FILE__,__FUNCTION__,__LINE__,unit->getId(),factionIndex);
		throw megaglest_runtime_error(szBuf);
	}
    const CommandType* ct= unit->getType()->findCommandTypeById(commandType->getId());
	if(ct == NULL) {
	    char szBuf[8096]="";
	    snprintf(szBuf,8096,"In [%s::%s Line: %d]\nCan not find AI command type for:\nunit = %d\n[%s]\n[%s]\nactual local factionIndex = %d.\nGame out of synch.",
            __FILE__,__FUNCTION__,__LINE__,
            unit->getId(), unit->getFullName(false).c_str(),unit->getDesc(false).c_str(),
            unit->getFaction()->getIndex());

	    if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"%s\n",szBuf);

	    std::string worldLog = world->DumpWorldToLog();
	    std::string sError = "worldLog = " + worldLog + " " + string(szBuf);
		throw megaglest_runtime_error(sError);
	}

	if(executeCommandOverNetwork() == true) {
		result = commander->tryGiveCommand(unit, commandType, pos,
				unit->getType(),CardinalDir(CardinalDir::NORTH), false, NULL,unitGroupCommandId);
		return result;
	}
	else {
		if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);

		Faction *faction = world->getFaction(unit->getFactionIndex());
		Unit *unitToCommand = faction->findUnit(unit->getId());
		Command *cmd = new Command(commandType, pos);
		cmd->setUnitCommandGroupId(unitGroupCommandId);
		result = unitToCommand->giveCommand(cmd);

		if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);
		return result;
	}
}

std::pair<CommandResult,string> AiInterface::giveCommand(int unitIndex, const CommandType *commandType, const Vec2i &pos, int unitGroupCommandId) {
	assert(this->gameSettings != NULL);

	std::pair<CommandResult,string> result(crFailUndefined,"");
	const Unit *unit = getMyUnit(unitIndex);
	if(unit == NULL) {
	    char szBuf[8096]="";
	    snprintf(szBuf,8096,"In [%s::%s Line: %d] Can not find AI unit with index: %d, AI factionIndex = %d. Game out of synch.",__FILE__,__FUNCTION__,__LINE__,unitIndex,factionIndex);
		throw megaglest_runtime_error(szBuf);
	}
    const UnitType* unitType= unit->getType();
	if(unitType == NULL) {
	    char szBuf[8096]="";
	    snprintf(szBuf,8096,"In [%s::%s Line: %d] Can not find AI unittype with unit index: %d, AI factionIndex = %d. Game out of synch.",__FILE__,__FUNCTION__,__LINE__,unitIndex,factionIndex);
		throw megaglest_runtime_error(szBuf);
	}
    const CommandType* ct= unit->getType()->findCommandTypeById(commandType->getId());
	if(ct == NULL) {
	    char szBuf[8096]="";
	    snprintf(szBuf,8096,"In [%s::%s Line: %d]\nCan not find AI command type for:\nunit = %d\n[%s]\n[%s]\nactual local factionIndex = %d.\nGame out of synch.",
            __FILE__,__FUNCTION__,__LINE__,
            unit->getId(), unit->getFullName(false).c_str(),unit->getDesc(false).c_str(),
            unit->getFaction()->getIndex());

	    if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"%s\n",szBuf);

	    std::string worldLog = world->DumpWorldToLog();
	    std::string sError = "worldLog = " + worldLog + " " + string(szBuf);
		throw megaglest_runtime_error(sError);
	}

	if(executeCommandOverNetwork() == true) {
		const Unit *unit = getMyUnit(unitIndex);
		result = commander->tryGiveCommand(unit, commandType, pos, unit->getType(),CardinalDir(CardinalDir::NORTH));
		return result;
	}
	else {
		if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);

		Command *cmd = new Command(commandType, pos);
		cmd->setUnitCommandGroupId(unitGroupCommandId);
		result = world->getFaction(factionIndex)->getUnit(unitIndex)->giveCommand(cmd);

		if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);
		return result;
	}
}

std::pair<CommandResult,string> AiInterface::giveCommand(int unitIndex, const CommandType *commandType, const Vec2i &pos, const UnitType *ut) {
	assert(this->gameSettings != NULL);

	std::pair<CommandResult,string> result(crFailUndefined,"");
	const Unit *unit = getMyUnit(unitIndex);
	if(unit == NULL) {
	    char szBuf[8096]="";
	    snprintf(szBuf,8096,"In [%s::%s Line: %d] Can not find AI unit with index: %d, AI factionIndex = %d. Game out of synch.",__FILE__,__FUNCTION__,__LINE__,unitIndex,factionIndex);
		throw megaglest_runtime_error(szBuf);
	}
    const UnitType* unitType= unit->getType();
	if(unitType == NULL) {
	    char szBuf[8096]="";
	    snprintf(szBuf,8096,"In [%s::%s Line: %d] Can not find AI unittype with unit index: %d, AI factionIndex = %d. Game out of synch.",__FILE__,__FUNCTION__,__LINE__,unitIndex,factionIndex);
		throw megaglest_runtime_error(szBuf);
	}
    const CommandType* ct= unit->getType()->findCommandTypeById(commandType->getId());
	if(ct == NULL) {
	    char szBuf[8096]="";
	    snprintf(szBuf,8096,"In [%s::%s Line: %d]\nCan not find AI command type for:\nunit = %d\n[%s]\n[%s]\nactual local factionIndex = %d.\nGame out of synch.",
            __FILE__,__FUNCTION__,__LINE__,
            unit->getId(), unit->getFullName(false).c_str(),unit->getDesc(false).c_str(),
            unit->getFaction()->getIndex());

	    if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"%s\n",szBuf);

	    std::string worldLog = world->DumpWorldToLog();
	    std::string sError = "worldLog = " + worldLog + " " + string(szBuf);
		throw megaglest_runtime_error(sError);
	}

	if(executeCommandOverNetwork() == true) {
		const Unit *unit = getMyUnit(unitIndex);
		result = commander->tryGiveCommand(unit, commandType, pos, ut,CardinalDir(CardinalDir::NORTH));
		return result;
	}
	else {
		if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);

		result = world->getFaction(factionIndex)->getUnit(unitIndex)->giveCommand(new Command(commandType, pos, ut, CardinalDir(CardinalDir::NORTH)));

		if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);

		return result;
	}
}

std::pair<CommandResult,string> AiInterface::giveCommand(int unitIndex, const CommandType *commandType, Unit *u){
	assert(this->gameSettings != NULL);
	assert(this->commander != NULL);

	std::pair<CommandResult,string> result(crFailUndefined,"");
	const Unit *unit = getMyUnit(unitIndex);
	if(unit == NULL) {
	    char szBuf[8096]="";
	    snprintf(szBuf,8096,"In [%s::%s Line: %d] Can not find AI unit with index: %d, AI factionIndex = %d. Game out of synch.",__FILE__,__FUNCTION__,__LINE__,unitIndex,factionIndex);
		throw megaglest_runtime_error(szBuf);
	}
    const UnitType* unitType= unit->getType();
	if(unitType == NULL) {
	    char szBuf[8096]="";
	    snprintf(szBuf,8096,"In [%s::%s Line: %d] Can not find AI unittype with unit index: %d, AI factionIndex = %d. Game out of synch.",__FILE__,__FUNCTION__,__LINE__,unitIndex,factionIndex);
		throw megaglest_runtime_error(szBuf);
	}
    const CommandType* ct= (commandType != NULL ? unit->getType()->findCommandTypeById(commandType->getId()) : NULL);
	if(ct == NULL) {
	    char szBuf[8096]="";
	    snprintf(szBuf,8096,"In [%s::%s Line: %d]\nCan not find AI command type for:\nunit = %d\n[%s]\n[%s]\nactual local factionIndex = %d.\nGame out of synch.",
            __FILE__,__FUNCTION__,__LINE__,
            unit->getId(), unit->getFullName(false).c_str(),unit->getDesc(false).c_str(),
            unit->getFaction()->getIndex());

	    if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"%s\n",szBuf);

	    std::string worldLog = world->DumpWorldToLog();
	    std::string sError = "worldLog = " + worldLog + " " + string(szBuf);
		throw megaglest_runtime_error(sError);
	}

	if(executeCommandOverNetwork() == true) {
		Unit *targetUnit = u;
		const Unit *unit = getMyUnit(unitIndex);

		result = commander->tryGiveCommand(unit, commandType, Vec2i(0), unit->getType(),CardinalDir(CardinalDir::NORTH),false,targetUnit);

		return result;
	}
	else {
		if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);

		result = world->getFaction(factionIndex)->getUnit(unitIndex)->giveCommand(new Command(commandType, u));

		if(SystemFlags::getSystemSettingType(SystemFlags::debugSystem).enabled) SystemFlags::OutputDebug(SystemFlags::debugSystem,"In [%s::%s Line: %d]\n",__FILE__,__FUNCTION__,__LINE__);

		return result;
	}
}

// ==================== get data ====================

int AiInterface::getMapMaxPlayers(){
     return world->getMaxPlayers();
}

Vec2i AiInterface::getHomeLocation(){
	return world->getMap()->getStartLocation(world->getFaction(factionIndex)->getStartLocationIndex());
}

Vec2i AiInterface::getStartLocation(int loactionIndex){
	return world->getMap()->getStartLocation(loactionIndex);
}

int AiInterface::getFactionCount(){
     return world->getFactionCount();
}

int AiInterface::getMyUnitCount() const{
	return world->getFaction(factionIndex)->getUnitCount();
}

int AiInterface::getMyUpgradeCount() const{
	return world->getFaction(factionIndex)->getUpgradeManager()->getUpgradeCount();
}

//int AiInterface::onSightUnitCount() {
//    int count=0;
//	Map *map= world->getMap();
//	for(int i=0; i<world->getFactionCount(); ++i) {
//		for(int j=0; j<world->getFaction(i)->getUnitCount(); ++j) {
//			Unit *unit = world->getFaction(i)->getUnit(j);
//			SurfaceCell *sc= map->getSurfaceCell(Map::toSurfCoords(unit->getPos()));
//			bool cannotSeeUnit = (unit->getType()->hasCellMap() == true &&
//								  unit->getType()->getAllowEmptyCellMap() == true &&
//								  unit->getType()->hasEmptyCellMap() == true);
//			if(sc->isVisible(teamIndex) && cannotSeeUnit == false) {
//				count++;
//			}
//		}
//	}
//    return count;
//}

const Resource *AiInterface::getResource(const ResourceType *rt){
	return world->getFaction(factionIndex)->getResource(rt);
}

Unit *AiInterface::getMyUnitPtr(int unitIndex) {
	if(unitIndex < 0 || unitIndex >= world->getFaction(factionIndex)->getUnitCount()) {
		char szBuf[8096]="";
		snprintf(szBuf,8096,"In [%s::%s Line: %d] unitIndex >= world->getFaction(factionIndex)->getUnitCount(), unitIndex = %d, world->getFaction(factionIndex)->getUnitCount() = %d",__FILE__,__FUNCTION__,__LINE__,unitIndex,world->getFaction(factionIndex)->getUnitCount());
		throw megaglest_runtime_error(szBuf);
	}

	return world->getFaction(factionIndex)->getUnit(unitIndex);
}

const Unit *AiInterface::getMyUnit(int unitIndex) {
	return getMyUnitPtr(unitIndex);
}

//const Unit *AiInterface::getOnSightUnit(int unitIndex) {
//
//    int count=0;
//	Map *map= world->getMap();
//
//	for(int i=0; i<world->getFactionCount(); ++i) {
//        for(int j=0; j<world->getFaction(i)->getUnitCount(); ++j) {
//            Unit * unit= world->getFaction(i)->getUnit(j);
//            SurfaceCell *sc= map->getSurfaceCell(Map::toSurfCoords(unit->getPos()));
//			bool cannotSeeUnit = (unit->getType()->hasCellMap() == true &&
//								  unit->getType()->getAllowEmptyCellMap() == true &&
//								  unit->getType()->hasEmptyCellMap() == true);
//
//            if(sc->isVisible(teamIndex)  && cannotSeeUnit == false) {
//				if(count==unitIndex) {
//					return unit;
//				}
//				else {
//					count ++;
//				}
//            }
//        }
//	}
//    return NULL;
//}

const FactionType * AiInterface::getMyFactionType(){
	return world->getFaction(factionIndex)->getType();
}

const ControlType AiInterface::getControlType(){
	return world->getFaction(factionIndex)->getControlType();
}

const TechTree *AiInterface::getTechTree(){
	return world->getTechTree();
}


bool AiInterface::isResourceInRegion(const Vec2i &pos, const ResourceType *rt, Vec2i &resourcePos, int range) const {
	const Map *map= world->getMap();

	int xi=1;
	int xj=1;

	if(rand() % 2==1){
		xi=-1;
	}
	if(rand() % 2==1){
		xj=-1;
	}
	for(int i = -range; i <= range; ++i) {
		for(int j = -range; j <= range; ++j) {
			int ii=xi*i;
			int jj=xj*j;
			if(map->isInside(pos.x + ii, pos.y + jj)) {
				Resource *r= map->getSurfaceCell(map->toSurfCoords(Vec2i(pos.x + ii, pos.y + jj)))->getResource();
				if(r != NULL) {
					if(r->getType() == rt) {
						resourcePos= pos + Vec2i(ii,jj);
						return true;
					}
				}
			}
		}
	}
	return false;
}



//returns if there is a resource next to a unit, in "resourcePos" is stored the relative position of the resource
bool AiInterface::isResourceNear(const Vec2i &pos, const ResourceType *rt, Vec2i &resourcePos, Faction *faction, bool fallbackToPeersHarvestingSameResource) const {
	const Map *map= world->getMap();
	int size = 1;
	for(int i = -1; i <= size; ++i) {
		for(int j = -1; j <= size; ++j) {
			if(map->isInside(pos.x + i, pos.y + j)) {
				Resource *r= map->getSurfaceCell(map->toSurfCoords(Vec2i(pos.x + i, pos.y + j)))->getResource();
				if(r != NULL) {
					if(r->getType() == rt) {
						resourcePos= pos + Vec2i(i,j);

						return true;
					}
				}
			}
		}
	}

	if(fallbackToPeersHarvestingSameResource == true && faction != NULL) {
		// Look for another unit that is currently harvesting the same resource
		// type right now

		// Check the faction cache for a known position where we can harvest
		// this resource type
		Vec2i result = faction->getClosestResourceTypeTargetFromCache(pos, rt);
		if(result.x >= 0) {
			resourcePos = result;

			if(pos.dist(resourcePos) <= size) {
				return true;
			}
		}
	}

	return false;
}

bool AiInterface::getNearestSightedResource(const ResourceType *rt, const Vec2i &pos,
											Vec2i &resultPos, bool usableResourceTypeOnly) {
	Faction *faction = world->getFaction(factionIndex);
	//float tmpDist=0;
	float nearestDist= infinity;
	bool anyResource= false;
	resultPos.x = -1;
	resultPos.y = -1;

	bool canUseResourceType = (usableResourceTypeOnly == false);
	if(usableResourceTypeOnly == true) {
		// can any unit harvest this resource yet?
		std::map<const ResourceType *,int>::iterator iterFind = cacheUnitHarvestResourceLookup.find(rt);

		if(	iterFind != cacheUnitHarvestResourceLookup.end() &&
			faction->findUnit(iterFind->second) != NULL) {
			canUseResourceType = true;
		}
		else {
			int unitCount = getMyUnitCount();
			for(int i = 0; i < unitCount; ++i) {
				const Unit *unit = getMyUnit(i);
				const HarvestCommandType *hct= unit->getType()->getFirstHarvestCommand(rt,unit->getFaction());
				if(hct != NULL) {
					canUseResourceType = true;
					cacheUnitHarvestResourceLookup[rt] = unit->getId();
					break;
				}
			}
		}
	}

	if(canUseResourceType == true) {
		bool isResourceClose = isResourceNear(pos, rt, resultPos, faction, true);

		// Found a resource
		if(isResourceClose == true || resultPos.x >= 0) {
			anyResource= true;
		}
		else {
			const Map *map		= world->getMap();
			for(int i = 0; i < map->getW(); ++i) {
				for(int j = 0; j < map->getH(); ++j) {
					Vec2i resPos = Vec2i(i, j);
					Vec2i surfPos= Map::toSurfCoords(resPos);
					SurfaceCell *sc = map->getSurfaceCell(surfPos);

					//if explored cell
					if(sc != NULL && sc->isExplored(teamIndex)) {
						Resource *r= sc->getResource();

						//if resource cell
						if(r != NULL) {
							if(r->getType() == rt) {
								float tmpDist= pos.dist(resPos);
								if(tmpDist < nearestDist) {
									anyResource= true;
									nearestDist= tmpDist;
									resultPos= resPos;
								}
							}
						}
					}
				}
			}
		}
	}
	return anyResource;
}

bool AiInterface::isAlly(const Unit *unit) const{
	return world->getFaction(factionIndex)->isAlly(unit->getFaction());
}
bool AiInterface::reqsOk(const RequirableType *rt){
    return world->getFaction(factionIndex)->reqsOk(rt);
}

bool AiInterface::reqsOk(const CommandType *ct){
    return world->getFaction(factionIndex)->reqsOk(ct);
}

bool AiInterface::checkCosts(const ProducibleType *pt, const CommandType *ct) {
	return world->getFaction(factionIndex)->checkCosts(pt,ct);
}

bool AiInterface::isFreeCells(const Vec2i &pos, int size, Field field){
    return world->getMap()->isFreeCells(pos, size, field);
}

void AiInterface::removeEnemyWarningPositionFromList(Vec2i &checkPos) {
	for(int i = (int)enemyWarningPositionList.size() - 1; i >= 0; --i) {
		Vec2i &pos = enemyWarningPositionList[i];

		if(checkPos == pos) {
			enemyWarningPositionList.erase(enemyWarningPositionList.begin()+i);
			break;
		}
	}
}

const Unit *AiInterface::getFirstOnSightEnemyUnit(Vec2i &pos, Field &field, int radius) {
	Map *map= world->getMap();

	const int CHECK_RADIUS = 12;
	const int WARNING_ENEMY_COUNT = 6;

	for(int i = 0; i < world->getFactionCount(); ++i) {
        for(int j = 0; j < world->getFaction(i)->getUnitCount(); ++j) {
            Unit * unit= world->getFaction(i)->getUnit(j);
            SurfaceCell *sc= map->getSurfaceCell(Map::toSurfCoords(unit->getPos()));
			bool cannotSeeUnit = (unit->getType()->hasCellMap() == true &&
								  unit->getType()->getAllowEmptyCellMap() == true &&
								  unit->getType()->hasEmptyCellMap() == true);

            if(sc->isVisible(teamIndex)  && cannotSeeUnit == false &&
               isAlly(unit) == false && unit->isAlive() == true) {
                pos= unit->getPos();
    			field= unit->getCurrField();
                if(pos.dist(getHomeLocation()) < radius) {
                    printLog(2, "Being attacked at pos "+intToStr(pos.x)+","+intToStr(pos.y)+"\n");

                    // Now check if there are more than x enemies in sight and if
                    // so make note of the position
                    int foundEnemies = 0;
                    std::map<int,bool> foundEnemyList;
                	for(int aiX = pos.x-CHECK_RADIUS; aiX < pos.x + CHECK_RADIUS; ++aiX) {
                		for(int aiY = pos.y-CHECK_RADIUS; aiY < pos.y + CHECK_RADIUS; ++aiY) {
                			Vec2i checkPos(aiX,aiY);
                			if(map->isInside(checkPos) && map->isInsideSurface(map->toSurfCoords(checkPos))) {
                				Cell *cAI = map->getCell(checkPos);
                				SurfaceCell *scAI = map->getSurfaceCell(Map::toSurfCoords(checkPos));
                				if(scAI != NULL && cAI != NULL && cAI->getUnit(field) != NULL && sc->isVisible(teamIndex)) {
                					const Unit *checkUnit = cAI->getUnit(field);
                					if(foundEnemyList.find(checkUnit->getId()) == foundEnemyList.end()) {
										bool cannotSeeUnitAI = (checkUnit->getType()->hasCellMap() == true &&
															checkUnit->getType()->getAllowEmptyCellMap() == true &&
															checkUnit->getType()->hasEmptyCellMap() == true);
										if(cannotSeeUnitAI == false && isAlly(checkUnit) == false
												&& checkUnit->isAlive() == true) {
											foundEnemies++;
											foundEnemyList[checkUnit->getId()] = true;
										}
                					}
                				}
                			}
                		}
                	}
                	if(foundEnemies >= WARNING_ENEMY_COUNT) {
                		if(std::find(enemyWarningPositionList.begin(),enemyWarningPositionList.end(),pos) == enemyWarningPositionList.end()) {
                			enemyWarningPositionList.push_back(pos);
                		}
                	}
                    return unit;
                }
            }
        }
	}
    return NULL;
}

Map * AiInterface::getMap() {
	Map *map= world->getMap();
	return map;
}

bool AiInterface::factionUsesResourceType(const FactionType *factionType, const ResourceType *rt) {
	bool factionUsesResourceType = factionType->factionUsesResourceType(rt);
	return factionUsesResourceType;
}

void AiInterface::saveGame(XmlNode *rootNode) const {
	std::map<string,string> mapTagReplacements;
	XmlNode *aiInterfaceNode = rootNode->addChild("AiInterface");

//    World *world;
//    Commander *commander;
//    Console *console;
//    GameSettings *gameSettings;
//
//    Ai ai;
	ai.saveGame(aiInterfaceNode);
//    int timer;
	aiInterfaceNode->addAttribute("timer",intToStr(timer), mapTagReplacements);
//    int factionIndex;
	aiInterfaceNode->addAttribute("factionIndex",intToStr(factionIndex), mapTagReplacements);
//    int teamIndex;
	aiInterfaceNode->addAttribute("teamIndex",intToStr(teamIndex), mapTagReplacements);
//	//config
//	bool redir;
	aiInterfaceNode->addAttribute("redir",intToStr(redir), mapTagReplacements);
//    int logLevel;
	aiInterfaceNode->addAttribute("logLevel",intToStr(logLevel), mapTagReplacements);
//    std::map<const ResourceType *,int> cacheUnitHarvestResourceLookup;
	for(std::map<const ResourceType *,int>::const_iterator iterMap = cacheUnitHarvestResourceLookup.begin();
			iterMap != cacheUnitHarvestResourceLookup.end(); ++iterMap) {
		XmlNode *cacheUnitHarvestResourceLookupNode = aiInterfaceNode->addChild("cacheUnitHarvestResourceLookup");

		cacheUnitHarvestResourceLookupNode->addAttribute("key",iterMap->first->getName(), mapTagReplacements);
		cacheUnitHarvestResourceLookupNode->addAttribute("value",intToStr(iterMap->second), mapTagReplacements);
	}
}

// AiInterface::AiInterface(Game &game, int factionIndex, int teamIndex, int useStartLocation) {
void AiInterface::loadGame(const XmlNode *rootNode, Faction *faction) {
	XmlNode *aiInterfaceNode = NULL;
	vector<XmlNode *> aiInterfaceNodeList = rootNode->getChildList("AiInterface");
	for(unsigned int i = 0; i < aiInterfaceNodeList.size(); ++i) {
		XmlNode *node = aiInterfaceNodeList[i];
		if(node->getAttribute("factionIndex")->getIntValue() == faction->getIndex()) {
			aiInterfaceNode = node;
			break;
		}
	}

	if(aiInterfaceNode != NULL) {
		factionIndex = aiInterfaceNode->getAttribute("factionIndex")->getIntValue();
		teamIndex = aiInterfaceNode->getAttribute("teamIndex")->getIntValue();

		ai.loadGame(aiInterfaceNode, faction);
		//firstTime = timeflowNode->getAttribute("firstTime")->getFloatValue();

		timer = aiInterfaceNode->getAttribute("timer")->getIntValue();
	//    int factionIndex;
		//factionIndex = aiInterfaceNode->getAttribute("factionIndex")->getIntValue();
	//    int teamIndex;
		//teamIndex = aiInterfaceNode->getAttribute("teamIndex")->getIntValue();
	//	//config
	//	bool redir;
		redir = aiInterfaceNode->getAttribute("redir")->getIntValue() != 0;
	//    int logLevel;
		logLevel = aiInterfaceNode->getAttribute("logLevel")->getIntValue();

	//    std::map<const ResourceType *,int> cacheUnitHarvestResourceLookup;
	//	for(std::map<const ResourceType *,int>::const_iterator iterMap = cacheUnitHarvestResourceLookup.begin();
	//			iterMap != cacheUnitHarvestResourceLookup.end(); ++iterMap) {
	//		XmlNode *cacheUnitHarvestResourceLookupNode = aiInterfaceNode->addChild("cacheUnitHarvestResourceLookup");
	//
	//		cacheUnitHarvestResourceLookupNode->addAttribute("key",iterMap->first->getName(), mapTagReplacements);
	//		cacheUnitHarvestResourceLookupNode->addAttribute("value",intToStr(iterMap->second), mapTagReplacements);
	//	}
	}
}

}}//end namespace
