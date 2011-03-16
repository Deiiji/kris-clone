/** 
 * @file llviewerxmppclient.cpp
 * @brief xmpp client
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"
#include "llagent.h"
#include "llbufferstream.h"
#include "llpumpio.h"
#include "lliosocket.h"
#include "llrefcount.h"
#include "llsecapi.h"
#include "llviewerxmppclient.h"
#include "llviewernetwork.h"
#include "llimview.h"
#include "llnotificationsutil.h"

// STONE XMPP HACK
#include "llimfloater.h"
#include "llpanelimcontrolpanel.h"
#include "llparticipantlist.h"

// Hack.
std::string server;

void LLViewerXMPPClient::setServer(const std::string& s)
{
	server = s;
}

LLViewerXMPPClient *LLViewerXMPPClient::connect(LLPointer<LLCredential> credential,
												apr_pool_t *pool)
{
	if (server.empty()) {
		LL_INFOS("XMPP") << "LLViewerXMPPClient server hostname is empty, not using XMPP" << LL_ENDL;
		return NULL;
	}

	LLSD identifier = credential->getIdentifier();
	std::string login_id = identifier["first_name"].asString();
	std::string password, auth_type;

	if ((std::string) identifier["type"] == "agent") 
	{
		std::string lastname = identifier["last_name"].asString();

	    if (!lastname.empty() && lastname != "Resident")
	    {
		    // support old-style first/last names
		    login_id += ".";
		    login_id += lastname;
	    }

		LLSD authenticator = credential->getAuthenticator();
		auth_type = authenticator["type"].asString();

		if (auth_type == CRED_AUTHENTICATOR_TYPE_HASH)
		{
			password = "$1$" + authenticator["secret"].asString();
		}
		else
	//	if (auth_type == CRED_AUTHENTICATOR_TYPE_CLEAR)
		{
			password = authenticator["secret"].asString();
		}
	}
	
	LL_INFOS("XMPP") << "connecting to " << server << " as " << login_id
					 << " with " << auth_type << " credential" << llendl;

	LLViewerXMPPClient *c = new LLViewerXMPPClient(server, 5222, false,
												   login_id, password, pool);

	gAgent.addGroupChangeCallback(
		boost::bind(&LLViewerXMPPClient::onGroupChange, c, _1, _2));

	return c;
}

LLViewerXMPPClient::LLViewerXMPPClient(const std::string& server, U16 port,
									   bool tls,
									   const std::string& username,
									   const std::string& password,
									   apr_pool_t *pool)
	: LLXMPPClient(server, port, tls, username, password, pool)
	, mOpenSessions()
{
	// This gets us callbacks when IM session windows are opened and closed
	LLIMMgr::getInstance()->addSessionObserver(this);
}


void LLViewerXMPPClient::onConnect()
{
	LLXMPPClient::onConnect();

	LL_INFOS("XMPP") << "LLViewerXMPPClient::onConnect()" << llendl;

	for (LLDynamicArray<LLGroupData>::const_iterator it =
			 gAgent.mGroups.begin(), end = gAgent.mGroups.end();
		 it != end; ++it)
	{
		join(it->mName, it->mID);
	}
}


void LLViewerXMPPClient::onGroupChange(const LLGroupData& gd,
									   GroupMembershipChange c)
{
	if (!connected())
		return;

	if (c == GroupAdd)
		join(gd.mName, gd.mID);
	else if (c == GroupRemove)
		leave(gd.mName);
}

/* Begin LLIMSessionObserver */
void LLViewerXMPPClient::sessionAdded(const LLUUID& session_id, const std::string& name, const LLUUID& other_participant_id)
{
	LL_DEBUGS("XMPP") << "sessionAdded(" << session_id
					  << ", " << name
					  << ", " << other_participant_id
					  << ")" << llendl;
	join(name, other_participant_id);
	mOpenSessions[session_id] = true;

	// The session window is opened, but we're not connected; let the user know.
	if (! connected())
	{
		gIMMgr->addMessage(session_id, LLUUID::null, SYSTEM_FROM,
						   "XMPP is currently disconnected.");
	}
}

void LLViewerXMPPClient::sessionRemoved(const LLUUID& session_id)
{
	LL_DEBUGS("XMPP") << "sessionRemoved(" << session_id << ")" << llendl;
	mOpenSessions[session_id] = false;
}

void LLViewerXMPPClient::sessionIDUpdated(const LLUUID& old_session_id, const LLUUID& new_session_id)
{
}
/* End LLIMSessionObserver */

LLViewerXMPPClient::~LLViewerXMPPClient()
{
}


void LLViewerXMPPClient::displayUnusualMessage(const std::string& type,
											   const std::string& subject,
											   const std::string& body)
{
	LL_INFOS("XMPP") << "Received unusual message"
					 << " type: " << type
					 << " subject: " << subject
					 << " body: " << body << llendl;
}

void LLViewerXMPPClient::displayGroupchatError(const inbound_xmpp_chat& iic)
{
	LL_INFOS("XMPP") << "received chat: '" << iic.utf8_text << "'" << llendl;
	LL_INFOS("XMPP") << "         from: '" << iic.from << "'" << llendl;
	LL_INFOS("XMPP") << "      from_id: '" << iic.from_id << "'" << llendl;
	LL_INFOS("XMPP") << "    from_nick: '" << iic.nickname << "'" << llendl;
	LL_INFOS("XMPP") << "    recipient: '" << iic.recipient << "'" << llendl;
	LL_INFOS("XMPP") << " recipient_id: '" << iic.recipient_id << "'" << llendl;

    if (! gIMMgr->hasSession (iic.recipient_id))
      gIMMgr->addSession (iic.recipient,
                          IM_SESSION_INVITE, /* IM_NOTHING_SPECIAL */
                          iic.recipient_id /*other_participant_id*/,
                          false);

	// gIMMgr->addMessage(it->first, LLUUID::null, SYSTEM_FROM,
	//				   "XMPP is currently disconnected.");

	gIMMgr->addMessage(iic.recipient_id, /* session_id */
					   iic.from_id, /* target_id (who talked) */
					   iic.nickname, /* from (name of who talked) */
					   "(xmpp error) " + iic.utf8_text, /* msg -- the text */
					   iic.recipient, /* session_name -- name of group */
					   IM_NOTHING_SPECIAL, /* EInstantMessage 13? XXX */
					   0, /* parent_estate_id */
					   LLUUID::null, /* region_id */
					   LLVector3 (0,0,0), /* position */
					   true /* link_name */);
}

void LLViewerXMPPClient::displayGroupchatMessage(const inbound_xmpp_chat& iic)
{
	LL_INFOS("XMPP") << "received chat: '" << iic.utf8_text << "'" << llendl;
	LL_INFOS("XMPP") << "         from: '" << iic.from << "'" << llendl;
	LL_INFOS("XMPP") << "      from_id: '" << iic.from_id << "'" << llendl;
	LL_INFOS("XMPP") << "    from_nick: '" << iic.nickname << "'" << llendl;
	LL_INFOS("XMPP") << "    recipient: '" << iic.recipient << "'" << llendl;
	LL_INFOS("XMPP") << " recipient_id: '" << iic.recipient_id << "'" << llendl;

    if (! gIMMgr->hasSession (iic.recipient_id))
      gIMMgr->addSession (iic.recipient,
                          IM_SESSION_INVITE, /* IM_NOTHING_SPECIAL */
                          iic.recipient_id /*other_participant_id*/,
                          false);

	std::string full_from = iic.from + " (" + iic.nickname + ")";

    /* llimview.cpp:2356 */
	gIMMgr->addMessage(iic.recipient_id, /* session_id */
					   iic.from_id, /* target_id (who talked) */
					   full_from, /* from (name of who talked) */
					   "(xmpp) " + iic.utf8_text, /* msg -- the text */
					   iic.recipient, /* session_name -- name of group */
					   IM_NOTHING_SPECIAL, /* EInstantMessage 13? XXX */
					   0, /* parent_estate_id */
					   LLUUID::null, /* region_id */
					   LLVector3 (0,0,0), /* position */
					   true /* link_name */);
}

void LLViewerXMPPClient::displayGroupchatSubject(const LLUUID& session_id,
												 const std::string& who,
												 const std::string& subject)
{
	LL_INFOS("XMPP") << " somebody [" << who << "] set up us the subject [" << subject << "]" << llendl;

    if (! gIMMgr->hasSession(session_id))
		return;

	LLIMFloater* floater = LLIMFloater::findInstance(session_id);
	if (!floater)
		return;

	floater->setSubject(subject);
}

// static
bool LLViewerXMPPClient::onDisconnectNotificationCB(const LLSD& notification,
													const LLSD& response)
{
	S32 option = LLNotificationsUtil::getSelectedOption(notification, response);

	if (option == 0)
	{
		// The next call to check_for_incoming_message will attempt a reconnect
		mRetryAttempt = 0;
	}

	return false;
}

void LLViewerXMPPClient::onMaxDisconnects()
{
	LLSD args;
	LLSD payload;
	args["ERROR"] = mLastError;
	LLNotificationsUtil::add("GroupChatDisconnected", args, payload,
							 boost::bind(&LLViewerXMPPClient::onDisconnectNotificationCB,
										 this, _1, _2));

	open_sessions_map::const_iterator it = mOpenSessions.begin();
	open_sessions_map::const_iterator end = mOpenSessions.end();
	for (; it != end; ++it)
	{
		if (! it->second)
			gIMMgr->addMessage(it->first, LLUUID::null, SYSTEM_FROM,
							   "XMPP is currently disconnected.");
			// TODO: also remove all xmpp roster entries.
	}
}

void LLViewerXMPPClient::onError(const std::string& message)
{
	LLXMPPClient::onError(message);

	LLSD args;
	args["ERROR"] = message;
	LLNotificationsUtil::add("GroupChatError", args);
}

void LLViewerXMPPClient::populateGroupPresence(const LLUUID& group_id,
											   LLPanelGroupControlPanel* const panel)
{
	LL_INFOS("XMPP") << "in populateGroupPresence for group " << group_id << llendl;

	LLGroupData gd;
	if (!gAgent.getGroupData(group_id, gd))
		return;
	LL_INFOS("XMPP") << "found data for group " << group_id << llendl;

	xmpp_group x_group;
	if (!getGroupData(gd.mName, x_group))
		return;
	LL_INFOS("XMPP") << "found an XMPP group structure " << llendl;

	if (!panel)
		return;
	LL_INFOS("XMPP") << "got a handle to a panel" << llendl;

	LLParticipantList *plist = panel->mParticipantList;
	if (!plist)
		return;
	LL_INFOS("XMPP") << "got a handle to a participant list" << llendl;

	LLAvatarList *alist = plist->mAvatarList;
	if (!alist)
		return;
	LL_INFOS("XMPP") << "got a handle to an avatar list " << llendl;

	room_jid_to_roster_map::const_iterator it = x_group.roster.begin();
	room_jid_to_roster_map::const_iterator end = x_group.roster.end();
	for (; it != end; ++it)
	{
		if (!it->second.online)
			continue;

		alist->addXMPPItem(it->second.agent_id, x_group.sl_session, it->second.nick);
	}
}

void LLViewerXMPPClient::displayPresence(const LLUUID& recipient_id,
										 const roster_item& roster_i,
										 bool modify_existing)
{
	// LLIMModel::LLIMSession* session = LLIMModel::getInstance()->findIMSession(recipient_id);
	// LLIMSpeakerMgr* speaker_mgr = LLIMModel::getInstance()->getSpeakerManager(recipient_id);

	// STONE XMPP HACK Deep hack of private stuff!

	LLIMFloater* floater = LLIMFloater::findInstance(recipient_id);
	if (!floater)
		return;
	LL_INFOS("XMPP") << "got a handle to a floater for recipient_id " << recipient_id << llendl;

	LLPanelGroupControlPanel *panel = (LLPanelGroupControlPanel *)floater->mControlPanel;
	if (!panel)
		return;
	LL_INFOS("XMPP") << "got a handle to a panel" << llendl;

	LLParticipantList *plist = panel->mParticipantList;
	if (!plist)
		return;
	LL_INFOS("XMPP") << "got a handle to a participant list" << llendl;

	LLAvatarList *alist = plist->mAvatarList;
	if (!alist)
		return;
	LL_INFOS("XMPP") << "got a handle to an avatar list " << llendl;

	std::string presence_msg;
	if (roster_i.online && modify_existing) {
		// alist->addXMPPItem(roster_i.agent_id, recipient_id, roster_i.nick);
		LL_DEBUGS("XMPP") << "TODO find and update this item: " << roster_i.jid << llendl;
		// FIXME: this isn't compiling!? const std::string& status = roster_i.presence.status;
		presence_msg += roster_i.jid + " has a new status in XMPP.";
	}
	else if (roster_i.online) {
		alist->addXMPPItem(roster_i.agent_id, recipient_id, roster_i.nick);
		LL_INFOS("XMPP") << "added the avatar list xmpp item: " << roster_i.jid << llendl;
		// FIXME: this isn't compiling!? const std::string& status = roster_i.presence.status;
		presence_msg += roster_i.jid + " is online in XMPP.";
	}
	else {
		alist->removeItemByValue(roster_i.jid);
		LL_INFOS("XMPP") << "removed the avatar list xmpp item: " << roster_i.jid << llendl;
		presence_msg += roster_i.jid + " is offline in XMPP.";
	}

	// gIMMgr->addSystemMessage(recipient_id, presence_msg, LLSD());
	gIMMgr->addMessage(recipient_id, LLUUID::null, SYSTEM_FROM, presence_msg);
	LL_INFOS("XMPP") << "added a system presence message to the chat window" << llendl;
}

/************************************************************************/
/*             class LLXMPPListItem                                  */
/************************************************************************/
LLXMPPListItem::LLXMPPListItem() : LLAvatarListItem(false)
{
	// should not use buildPanel from the base class to ensure LLXMPPListItem::postBuild is called.
	buildFromFile( "panel_avatar_list_item.xml");
}

BOOL LLXMPPListItem::postBuild()
{
	BOOL rv = LLAvatarListItem::postBuild();

	if (rv)
	{
		setOnline(true);
		showExtraInformation(false);   // S21.
		setShowProfileBtn(false);
		setShowInfoBtn(false);
		mAvatarIcon->setValue("XMPP_Icon");
		mAvatarIcon->setToolTip(std::string(""));
	}
	return rv;
}

// to work correctly this method should be called AFTER setAvatarId for avaline callers with hidden phone number
void LLXMPPListItem::setName(const std::string& name)
{
	LLAvatarListItem::setAvatarName(name);
	LLAvatarListItem::setAvatarToolTip(name);
}
