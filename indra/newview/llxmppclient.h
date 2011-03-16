/** 
 * @file llxmppclient.h
 * @brief hooks into an xmpp client library
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

#ifndef LLXMPPCLIENT_H
#define LLXMPPCLIENT_H


#include <strophe/strophe.h>
#include "llviewerprecompiledheaders.h"

class LLAvatarName;
namespace LLAvatarNameCache { class key; }

enum llxmpp_status_t { LLXMPP_DISCONNECTED, LLXMPP_PENDING, LLXMPP_CONNECTED };


enum llxmpp_presence_t {
	LLXMPP_PRESENCE_AVAILABLE,
	LLXMPP_PRESENCE_UNAVAILABLE,
	LLXMPP_PRESENCE_SUBSCRIBE,
	LLXMPP_PRESENCE_SUBSCRIBED,
	LLXMPP_PRESENCE_UNSUBSCRIBE,
	LLXMPP_PRESENCE_UNSUBSCRIBED,
	LLXMPP_PRESENCE_PROBE,
	LLXMPP_PRESENCE_ERROR
};

// N.B. these are specified in XMPP; translate before displaying, but not over-the-wire.
const char * const llxmpp_presence_text[] = {
	"available", // or empty
	"unavailable",
	"subscribe",
	"subscribed",
	"unsubscribe",
	"unsubscribed",
	"probe",
	"error",
	NULL
};


enum llxmpp_show_t {
	LLXMPP_SHOW_AVAILABLE,
	LLXMPP_SHOW_AWAY,
	LLXMPP_SHOW_CHATLABLE,
	LLXMPP_SHOW_DND,
	LLXMPP_SHOW_XA,
};

// N.B. these are specified in XMPP; translate before displaying, but not over-the-wire.
const char * const llxmpp_show_text[] = {
	"online", // or empty // The entity or resource is available.
	"away", // The entity or resource is temporarily away.
	"chat", // The entity or resource is actively interested in chatting.
	"dnd",  // The entity or resource is busy (dnd = "Do Not Disturb").
	"xa",   // The entity or resource is away for an extended period (xa = "eXtended Away").
	NULL
};


struct inbound_xmpp_chat
{
	inbound_xmpp_chat(const std::string& f,
					  const std::string& n,
					  const std::string& r,
					  const LLUUID& rid,
					  const std::string& b) :
		from(f), from_id(), nickname(n), recipient(r), recipient_id(rid), utf8_text(b)
		{
			LL_DEBUGS("XMPP") << "new iic, from=" << from << llendl;
			LL_DEBUGS("XMPP") << "new iic, from nick=" << nickname << llendl;
			LL_DEBUGS("XMPP") << "new iic, recipient=" << recipient << llendl;
			LL_DEBUGS("XMPP") << "new iic, recipient_id=" << recipient_id << llendl;
			LL_DEBUGS("XMPP") << "new iic, utf8_text=" << utf8_text << llendl;
		}

	inbound_xmpp_chat(const std::string& f,
					  const LLUUID& fid,
					  const std::string& n,
					  const std::string& r,
					  const LLUUID& rid,
					  const std::string& b) :
		from(f), from_id(fid), nickname(n), recipient(r), recipient_id(rid), utf8_text(b)
		{
			LL_DEBUGS("XMPP") << "new iic, from=" << from << llendl;
			LL_DEBUGS("XMPP") << "new iic, from nick=" << nickname << llendl;
			LL_DEBUGS("XMPP") << "new iic, from id=" << from_id << llendl;
			LL_DEBUGS("XMPP") << "new iic, recipient=" << recipient << llendl;
			LL_DEBUGS("XMPP") << "new iic, recipient_id=" << recipient_id << llendl;
			LL_DEBUGS("XMPP") << "new iic, utf8_text=" << utf8_text << llendl;
		}
	
	const std::string from;
	LLUUID from_id;
	const std::string nickname;
	const std::string recipient;
	const LLUUID recipient_id;
	const std::string utf8_text;
};


struct presence_info
{
	std::string status;
	llxmpp_show_t show;
	llxmpp_presence_t presence;
	S32 priority;

	presence_info() :
		status(),
		show(LLXMPP_SHOW_AVAILABLE),
		presence(LLXMPP_PRESENCE_AVAILABLE),
		priority(0) {}
};

struct roster_item
{
	std::string affiliation;
	std::string role;
	std::string jid;
	std::string nick;
	presence_info presence;
	LLUUID agent_id;
	bool online;

	roster_item(const std::string& a,
				const std::string& r,
				const std::string& j,
				const std::string& n,
				const presence_info& pi,
				bool o) :
		affiliation(a), role(r), jid(j), nick(n), presence(pi), agent_id(), online(o) { }
	roster_item() :
		affiliation(), role(), jid(), nick(), presence(), agent_id(), online(false) { }
};


typedef std::map<std::string, roster_item> room_jid_to_roster_map;


struct xmpp_group
{
  std::string name;
  LLUUID sl_session;
  std::string group_jid;
  std::string my_room_jid;
  room_jid_to_roster_map roster;

  xmpp_group (const std::string &n, const LLUUID &s, const std::string &j, const std::string &r)
	: name(n), sl_session(s), group_jid(j), my_room_jid(r) { }
  xmpp_group() : name(), sl_session(), group_jid(), my_room_jid() { }
};


typedef std::map<std::string, xmpp_group> group_name_to_data_map;


class LLXMPPClient
{
public:
	LLXMPPClient(const std::string& server, U16 port,
				 bool tls,
				 const std::string& username,
				 const std::string& password,
				 apr_pool_t *pool);
	virtual ~LLXMPPClient();

	bool connect();
	bool receive_chat(const std::string& from,
					  const std::string& recipient,
					  const std::string& utf8_text);

	bool join(const std::string& channel, const LLUUID& session_id);
	bool say(const std::string& channel, const std::string& message);
	bool leave(const std::string& channel);
	bool check_for_inbound_chat();

	virtual void displayPresence(const LLUUID& recipient_id,
								 const roster_item& roster_i,
								 bool modify_existing);

	virtual void onConnect ();
	virtual void onDisconnect ();
	virtual void onMaxDisconnects();
	virtual void onError(const std::string& message);

	virtual void displayUnusualMessage(const std::string& type,
								  	   const std::string& subject,
								  	   const std::string& body);

	virtual void displayGroupchatError(const inbound_xmpp_chat& iic);
	virtual void displayGroupchatMessage(const inbound_xmpp_chat& iic);
	virtual void displayGroupchatSubject(const LLUUID& session_id,
										 const std::string& who,
										 const std::string& subject);

	void handleGroupchatMessage(char *from_jid, char *subject, char *body);
	void handleChatMessage(char *from_jid, char *subject, char *body);
	void handleHeadlineMessage(char *from_jid, char *subject, char *body);
	void handleNormalMessage(char *from_jid, char *subject, char *body);
	void handleErrorMessage(char *from_jid, char *subject, char *body,
							char *errtype, char *errword);

	void handlePresence(char *room_jid, char *real_jid, char *affiliation, char *role,
						presence_info& pi, bool online);

	bool disconnected() const { return mConnected == LLXMPP_DISCONNECTED; }
	bool connected() const { return mConnected == LLXMPP_CONNECTED; }
	bool pending() const { return mConnected == LLXMPP_PENDING; }

	xmpp_ctx_t *mCtx;

protected:
	S32 mRetryAttempt;
	F64 mLastRetryTime;
	std::string mLastError;

	bool getGroupData(const std::string& name,
					  xmpp_group& group,
					  bool del = false);

private:
	llxmpp_status_t mConnected;

	std::string mUsername;
	std::string mPassword;
	std::string mServer;
	std::string mJid;

	xmpp_conn_t *mConn;
	xmpp_log_t mLog;

	apr_pool_t *mPool;

	void handleInboundChat(inbound_xmpp_chat *iic,
						   const LLAvatarNameCache::key& key,
						   const LLAvatarName& av_name);

	void setRosterAgentID(roster_item *roster_i,
						  const LLAvatarNameCache::key& key,
						  const LLAvatarName& av_name);

	void sendPresence(llxmpp_presence_t status);
	void sendPresence(llxmpp_presence_t status,
					  const std::string& to);
	void sendPresence(llxmpp_presence_t status,
					  const std::string& from,
					  const std::string& to);

	group_name_to_data_map mGroup_name_to_data;
};


#endif // LLXMPPCLIENT_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:t
  tab-width:4
  fill-column:99
  End:
*/
