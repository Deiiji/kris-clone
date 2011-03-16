/**
 * @file llviewerxmppclient.h
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

#ifndef LLVIEWERXMPPCLIENT_H
#define LLVIEWERXMPPCLIENT_H

// STONE XMPP HACK:
#include "llavatariconctrl.h"
#include "llavatarlistitem.h"
#include "llpanelimcontrolpanel.h"

#include "llimview.h" // LLIMSessionObserver
#include "llagentdata.h"
#include "llxmppclient.h"

struct LLGroupData;

typedef std::map<LLUUID, bool> open_sessions_map;

class LLViewerXMPPClient
	: public LLXMPPClient
	, public LLIMSessionObserver
{
public:
	static LLViewerXMPPClient *connect(LLPointer<LLCredential> credential,
									   apr_pool_t *pool);

	static void setServer(const std::string& s);

	virtual ~LLViewerXMPPClient();

	void displayGroupchatError(const inbound_xmpp_chat& iic);
	void displayGroupchatMessage(const inbound_xmpp_chat& iic);
	void displayGroupchatSubject(const LLUUID& session_id,
								 const std::string& who,
								 const std::string& subject);

	void displayPresence(const LLUUID& recipient_id,
						 const roster_item& roster_i,
						 bool modify_existing);

	void displayUnusualMessage(const std::string& type,
							   const std::string& subject,
							   const std::string& body);

	void onGroupChange(const LLGroupData& gd, GroupMembershipChange c);
	void onConnect();
	void onMaxDisconnects();
	void onError(const std::string& message);

	void populateGroupPresence(const LLUUID& group_id,
							   LLPanelGroupControlPanel* const panel);

	// LLIMSessionObserver observe triggers
	void sessionAdded(const LLUUID& session_id, const std::string& name, const LLUUID& other_participant_id);
	void sessionRemoved(const LLUUID& session_id);
	void sessionIDUpdated(const LLUUID& old_session_id, const LLUUID& new_session_id);

protected:
	LLViewerXMPPClient(const std::string& server, U16 port,
					   bool tls,
					   const std::string& username,
					   const std::string& password,
					   apr_pool_t *pool);

private:
	bool onDisconnectNotificationCB(const LLSD& notification,
									const LLSD& response);

	open_sessions_map mOpenSessions;
};

extern LLViewerXMPPClient *xmpp;

/**
 * Represents XMPP caller in Avatar list in Voice Control Panel and group chats.
 * Based on LLAvalineListItem subclass in llavatarlist.cpp
 */
class LLXMPPListItem : public LLAvatarListItem
{
public:

	/**
	 * Constructor
	 */
	LLXMPPListItem();

	/*virtual*/ BOOL postBuild();

	/*virtual*/ void setName(const std::string& name);
};

#endif // LLVIEWERXMPPCLIENT_H
