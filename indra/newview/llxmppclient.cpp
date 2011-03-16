/** 
 * @file llxmppclient.cpp
 * @brief hooks into strophe xmpp library
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
#include "llxmppclient.h"
#include <cctype>
#include <queue>
#include <map>
#include <utility>
#include <iostream>
#include "linden_common.h"
#include "llavatarnamecache.h"
#include "llstatusbar.h"
#include "llbufferstream.h"
#include "llframetimer.h"
#include "llhost.h"
#include "lliosocket.h"
#include "llmemtype.h"
#include "llpumpio.h"
#include "llversionviewer.h"
#include "llapr.h"
#include "apr_poll.h"
#include "apr_pools.h"
#include "apr_portable.h"


#include <strophe/strophe.h>
#include <strophe/common.h>

static std::map<llxmpp_status_t, std::string> llxmpp_status_text;
static S32 MAX_RETRIES = 5;
static S32 BACKOFF_SECONDS = 5;

static bool forbidden(unsigned char c)
{
	return c < 0x20 || c >= 0x7f || strchr("&'/:<>@-%", c) != NULL;
}


static std::string sl_name_to_xmpp(const std::string &sl_name)
{
	static const char *hex = "0123456789abcdef";
	std::string out;

	for (unsigned i = 0; i < sl_name.length(); i++)
	{
		unsigned char c = sl_name[i];
		
		if (forbidden(c)) {
			out.push_back('%');
			out.push_back(hex[c >> 4]);
			out.push_back(hex[c & 0xf]);
		}
		else if (c == ' ')
			out.push_back('-');
		else
			out.push_back(tolower(c));
	}

	return out;
}


static int parse_hex(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'a' && c <= 'f')
		return c - 'a';
	else if (c >= 'A' && c <= 'F')
		return c - 'A';
	else
		return -1;
}


static std::string xmpp_name_to_sl(const std::string &xmpp_name)
{
	std::string out;

	for (unsigned i = 0; i < xmpp_name.length(); i++)
    {
		int c = xmpp_name[i];
		
		if (c == '%') {
			if (i + 2 < xmpp_name.length()) {
				c = parse_hex(xmpp_name[i+1]) << 4 | parse_hex(xmpp_name[i+2]);
				i += 2;
			}
			else
				c = -1;
		}
		else if (c == '-')
			c = ' ';
		else if (forbidden(c))
			c = -1;

		if (c != -1)
			out.push_back(c);
    }

	return out;
}



static int version_handler(xmpp_conn_t *const conn,
						   xmpp_stanza_t *const stanza,
						   void *const userdata)
{
	xmpp_stanza_t *reply, *query, *name, *version, *text;
	char *ns;
	LLXMPPClient *xmpp_client = (LLXMPPClient *) userdata;
	xmpp_ctx_t *ctx = xmpp_client->mCtx;
	LL_INFOS("XMPP") << "Received version request from "
					 << xmpp_stanza_get_attribute (stanza, "from") << llendl;

	reply = xmpp_stanza_new (ctx);
	xmpp_stanza_set_name (reply, "iq");
	xmpp_stanza_set_type (reply, "result");
	xmpp_stanza_set_id (reply, xmpp_stanza_get_id (stanza));
	xmpp_stanza_set_attribute (reply, "to",
							   xmpp_stanza_get_attribute (stanza, "from"));

	query = xmpp_stanza_new (ctx);
	xmpp_stanza_set_name (query, "query");
	ns = xmpp_stanza_get_ns (xmpp_stanza_get_children (stanza));
	if (ns)
    {
		xmpp_stanza_set_ns (query, ns);
    }

	name = xmpp_stanza_new (ctx);
	xmpp_stanza_set_name (name, "name");
	xmpp_stanza_add_child (query, name);

	text = xmpp_stanza_new (ctx);
	xmpp_stanza_set_text (text, "secondlife viewer");
	xmpp_stanza_add_child (name, text);

	version = xmpp_stanza_new (ctx);
	xmpp_stanza_set_name (version, "version");
	xmpp_stanza_add_child (query, version);

	text = xmpp_stanza_new (ctx);
	xmpp_stanza_set_text (text, "1.0");
	xmpp_stanza_add_child (version, text);

	xmpp_stanza_add_child (reply, query);

	xmpp_send (conn, reply);
	xmpp_stanza_release (reply);
	return 1;
}

static llxmpp_presence_t presence_to_enum(char *presence)
{
	if (presence)
		for (int i = 0; llxmpp_presence_text[i]; ++i)
			if (strcmp(presence, llxmpp_presence_text[i]) == 0)
				return (llxmpp_presence_t)i;
	return LLXMPP_PRESENCE_AVAILABLE;
}

static llxmpp_show_t show_to_enum(char *show)
{
	if (show)
		for (int i = 0; llxmpp_show_text[i]; ++i)
			if (strcmp(show, llxmpp_show_text[i]) == 0)
				return (llxmpp_show_t)i;
	return LLXMPP_SHOW_AVAILABLE;
}

static int presence_handler(xmpp_conn_t *const conn,
							xmpp_stanza_t *const stanza,
							void *const userdata)
{
	LLXMPPClient *xmpp_client = (LLXMPPClient *) userdata;
	presence_info pi = presence_info();

	LL_INFOS("XMPP") << "in presence_handler." << llendl;

	/* <presence/> stanza attributes */
	char *from_jid = xmpp_stanza_get_attribute(stanza, "from");
	char *to_jid = xmpp_stanza_get_attribute(stanza, "to");
	char *type = xmpp_stanza_get_attribute(stanza, "type");

	pi.presence = presence_to_enum(type);
	bool online = (pi.presence != LLXMPP_PRESENCE_UNAVAILABLE);

	/* <show/> stanza */
	xmpp_stanza_t *show_stanza = xmpp_stanza_get_child_by_name(stanza, "show");
	if (show_stanza)
	{
		char *p_show = xmpp_stanza_get_text(show_stanza);
		pi.show = show_to_enum(p_show);
		xmpp_free(xmpp_client->mCtx, p_show);
	}

	/* <status/> stanza */
	xmpp_stanza_t *status_stanza = xmpp_stanza_get_child_by_name(stanza, "status");
	if (status_stanza)
	{
		char *p_status = xmpp_stanza_get_text(status_stanza);
		if (p_status) pi.status = p_status;
		xmpp_free(xmpp_client->mCtx, p_status);
	}

	/* <priority/> stanza */
	xmpp_stanza_t *priority_stanza = xmpp_stanza_get_child_by_name(stanza, "priority");
	if (priority_stanza)
	{
		char *p_priority = xmpp_stanza_get_text(priority_stanza);
		pi.priority = strtol(p_priority, NULL, 10);
		xmpp_free(xmpp_client->mCtx, p_priority);
	}

	LL_INFOS("XMPP") << "    presence pi.show - " << pi.show << llendl;
	LL_INFOS("XMPP") << "    presence pi.status - " << pi.status << llendl;
	LL_INFOS("XMPP") << "    presence pi.presence - " << pi.presence << llendl;
	LL_INFOS("XMPP") << "    presence pi.priority - " << pi.priority << llendl;

	/* <x/> and <item/> stanza */
	xmpp_stanza_t *x_stanza = xmpp_stanza_get_child_by_ns(stanza, "http://jabber.org/protocol/muc#user");
	if (! x_stanza)
		return 1;

	LL_INFOS("XMPP") << "    found x stanza with xmlns \"http://jabber.org/protocol/muc#user\"" << llendl;
	xmpp_stanza_t *item_stanza = xmpp_stanza_get_child_by_name(x_stanza, "item");
	if (! item_stanza)
		return 1;

	LL_INFOS("XMPP") << "    found item stanza" << llendl;

	/* presence information about this other person in the room */
	char *p_affiliation = xmpp_stanza_get_attribute(item_stanza, "affiliation");
	char *p_role = xmpp_stanza_get_attribute(item_stanza, "role");
	/* If p_jid is null, that's because we're in a semi-anonymous room, and we
	 * aren't a moderator. This use case is not supported in SL. */
	char *p_jid = xmpp_stanza_get_attribute(item_stanza, "jid");

	/* This is only for presence that we assert about ourselves, not others */
	char *code = NULL;
	xmpp_stanza_t *x_status_stanza = xmpp_stanza_get_child_by_name(x_stanza, "status");
	if (x_status_stanza) {
		code = xmpp_stanza_get_attribute(x_status_stanza, "code");
		LL_INFOS("XMPP") << "    our own presence status code: " << code << llendl;
	}

	LL_INFOS("XMPP") << "    presence from - " << from_jid << llendl;
	LL_INFOS("XMPP") << "    presence to - " << to_jid << llendl;
	LL_INFOS("XMPP") << "    presence affiliation - " << p_affiliation << llendl;
	LL_INFOS("XMPP") << "    presence role - " << p_role << llendl;
	LL_INFOS("XMPP") << "    presence jid - " << p_jid << llendl;

	xmpp_client->handlePresence(from_jid, p_jid, p_affiliation, p_role, pi, online);

	return 1;
}


static int message_handler(xmpp_conn_t *const conn,
						   xmpp_stanza_t *const stanza,
						   void *const userdata)
{
	char *subject = NULL;
	char *body = NULL;

	LLXMPPClient *xmpp_client = (LLXMPPClient *) userdata;

	LL_DEBUGS("XMPP") << "in message_handler." << llendl;

	char *type_attr = xmpp_stanza_get_attribute(stanza, "type");
	char *from_attr = xmpp_stanza_get_attribute(stanza, "from");
	if (! from_attr)
		return 1;

	LL_DEBUGS("XMPP") << "    handling message type " << type_attr
					  << " from " << from_attr << llendl;

	xmpp_stanza_t *subject_stanza = xmpp_stanza_get_child_by_name(stanza, "subject");
	if (subject_stanza)
		subject = xmpp_stanza_get_text(subject_stanza);

	xmpp_stanza_t *body_stanza = xmpp_stanza_get_child_by_name(stanza, "body");
	if (body_stanza)
		body = xmpp_stanza_get_text(body_stanza);

	// Comments are excepted from RFC 3921, Section 2.1.1
	if (type_attr && !strcmp(type_attr, "chat"))
	{
		/* chat -- The message is sent in the context of a one-to-one chat
		 * conversation. A compliant client SHOULD present the message in an
		 * interface enabling one-to-one chat between the two parties,
		 * including an appropriate conversation history. */
		xmpp_client->handleChatMessage(from_attr, subject, body);
		LL_DEBUGS("XMPP") << "    handleChatMessage called" << llendl;
	}
	else if (type_attr && !strcmp(type_attr, "error"))
	{
		/* error -- An error has occurred related to a previous message sent by
		 * the sender (for details regarding stanza error syntax, refer to
		 * [XMPP‑CORE]). A compliant client SHOULD present an appropriate
		 * interface informing the sender of the nature of the error. */

		/*
		<message
		    from='darkcave@chat.shakespeare.lit'
		    to='wiccarocks@shakespeare.lit/laptop'
		    type='error'>
		  <body>I'll give thee a wind.</body>
		  <error type='modify'>
		    <bad-request xmlns=''/>
		  </error>
		</message>
		 */

		char *errtype = NULL;
		char *errword = NULL;

		xmpp_stanza_t *type_stanza = xmpp_stanza_get_child_by_name(stanza, "error");
		if (type_stanza)
			errtype = xmpp_stanza_get_attribute(type_stanza, "type");

		xmpp_stanza_t *error_stanza = xmpp_stanza_get_child_by_ns(stanza, "urn:ietf:params:xml:ns:xmpp-stanzas");
		if (error_stanza)
			errword = xmpp_stanza_get_name(error_stanza);

		xmpp_client->handleErrorMessage(from_attr, subject, body, errtype, errword);
		LL_DEBUGS("XMPP") << "    handleErrorMessage called" << llendl;
	}
	else if (type_attr && !strcmp(type_attr, "groupchat"))
	{
		/* groupchat -- The message is sent in the context of a multi-user chat
		 * environment (similar to that of [IRC]). A compliant client SHOULD
		 * present the message in an interface enabling many-to-many chat
		 * between the parties, including a roster of parties in the chatroom
		 * and an appropriate conversation history. Full definition of
		 * XMPP-based groupchat protocols is out of scope for this memo (for
		 * details see [JEP‑0045]). */
		xmpp_client->handleGroupchatMessage(from_attr, subject, body);
		LL_DEBUGS("XMPP") << "    handleGroupchatMessage called" << llendl;
	}
	else if (type_attr && !strcmp(type_attr, "headline"))
	{
		/* headline -- The message is probably generated by an automated
		 * service that delivers or broadcasts content (news, sports, market
		 * information, RSS feeds, etc.). No reply to the message is expected,
		 * and a compliant client SHOULD present the message in an interface
		 * that appropriately differentiates the message from standalone
		 * messages, chat sessions, or groupchat sessions (e.g., by not
		 * providing the recipient with the ability to reply). */
		xmpp_client->handleHeadlineMessage(from_attr, subject, body);
		LL_DEBUGS("XMPP") << "    handleHeadlineMessage called" << llendl;
	}
	else // default: if (!strcmp(type_attr, "normal"))
	{
		/* normal -- The message is a single message that is sent outside the
		 * context of a one-to-one conversation or groupchat, and to which it
		 * is expected that the recipient will reply. A compliant client SHOULD
		 * present the message in an interface enabling the recipient to reply,
		 * but without a conversation history. */
		xmpp_client->handleNormalMessage(from_attr, subject, body);
		LL_DEBUGS("XMPP") << "    handleNormalMessage called" << llendl;
	}

	xmpp_free(xmpp_client->mCtx, subject);
	xmpp_free(xmpp_client->mCtx, body);

	return 1;
}


static void xmpp_connection_handler(xmpp_conn_t *const conn,
									const xmpp_conn_event_t status,
									const int error,
									xmpp_stream_error_t *const stream_error,
									void * const userdata)
{
	LLXMPPClient *xmpp_client = (LLXMPPClient *) userdata;
	LL_INFOS("XMPP") << "xmpp_connection_handler: " << (int)status << llendl;
	if (status == XMPP_CONN_CONNECT)
		xmpp_client->onConnect ();
	else
		xmpp_client->onDisconnect ();
}


// see typedef xmpp_log_handler in strophe.h
static void xmpp_logging_handler(void * const userdata, 
								 const xmpp_log_level_t level,
								 const char * const area,
								 const char * const msg)
{
	LLXMPPClient *xmpp_client = (LLXMPPClient *) userdata;

	switch (level) {
	case XMPP_LEVEL_DEBUG:
		LL_DEBUGS2("XMPP", area) << msg << LL_ENDL;
		break;
	case XMPP_LEVEL_INFO:
		LL_INFOS2("XMPP", area) << msg << LL_ENDL;
		// Some connection errors come back at the info level, uh.
		xmpp_client->onError(msg);
		break;
	default:
	case XMPP_LEVEL_WARN:
	case XMPP_LEVEL_ERROR:
		// Nothing in libstrophe at the ERROR level is fatal, but our
		// ERR level is, so report these at WARN level.
		LL_WARNS2("XMPP", area) << msg << LL_ENDL;
		xmpp_client->onError(msg);
		break;
	}
}


LLXMPPClient::LLXMPPClient(const std::string& server, U16 port,
						   bool tls,
						   const std::string& username,
						   const std::string& password,
						   apr_pool_t *pool)
	:
	  mConnected(LLXMPP_DISCONNECTED),
	  mRetryAttempt(0),
	  mLastRetryTime(0),
	  mPassword(password),
	  mServer(server),
	  mPool(pool)
{
	mUsername = sl_name_to_xmpp(username);

	llxmpp_status_text[LLXMPP_DISCONNECTED] = "XMPP Disconnected";
	llxmpp_status_text[LLXMPP_PENDING] = "XMPP connecting...";
	llxmpp_status_text[LLXMPP_CONNECTED] = "XMPP Connected!";

	char version[ 40 ];
	snprintf(version, sizeof (version), "%d.%d.%d",
			 LL_VERSION_MAJOR,
			 LL_VERSION_MINOR,
			 LL_VERSION_PATCH);


	mJid = mUsername + "@" + mServer + "/secondlife-viewer-" + version;
	xmpp_initialize ();
	mLog.handler = xmpp_logging_handler;
	mLog.userdata = this;

	connect ();
}


LLXMPPClient::~LLXMPPClient()
{
	xmpp_conn_release (mConn);
	xmpp_ctx_free (mCtx);
	xmpp_shutdown ();
}


void LLXMPPClient::onMaxDisconnects()
{
}


bool LLXMPPClient::connect()
{
	if (mRetryAttempt > MAX_RETRIES)
		return false;

	if (mRetryAttempt == MAX_RETRIES)
	{
		++mRetryAttempt; // Push it over the edge to stop error messages
		LL_INFOS("XMPP") << "Hit max retries, won't try to connect anymore." << llendl;
		onMaxDisconnects();
		return false;
	}

	F64 now = LLFrameTimer::getTotalSeconds();
	F64 retry_delay = now - mLastRetryTime;

	// If we're on the 2-Nth retry, don't continue until some time has elapsed.
	if (mRetryAttempt > 0 && retry_delay < mRetryAttempt * BACKOFF_SECONDS)
		return false;

	LL_INFOS("XMPP") << "Attempting to connect to XMPP server, backed off " << retry_delay
					 << " seconds, try number " << mRetryAttempt << llendl;

	mLastRetryTime = now;
	mCtx = xmpp_ctx_new (NULL, &mLog);
	mConn = xmpp_conn_new (mCtx);
	xmpp_conn_set_jid (mConn, mJid.c_str ());
	xmpp_conn_set_pass (mConn, mPassword.c_str ());
	if (xmpp_connect_client (mConn,
							 mServer.c_str (), 5222,
							 xmpp_connection_handler, this) == 0)
	{
		mConnected = LLXMPP_PENDING;
		return true;
	} else {
		mConnected = LLXMPP_DISCONNECTED;
		return false;
	}
}


bool LLXMPPClient::join(const std::string& channel, const LLUUID& session_id)
{
	xmpp_group group;
	if (getGroupData(channel, group))
    {
		LL_INFOS("XMPP") << "already present in group chat " << channel
						 << llendl;
		return true;
    }

	std::string muc = sl_name_to_xmpp(channel);
	std::string client_muc_jid =
		muc + "@conference." + mServer + "/" + mUsername;
	std::string muc_jid = muc + "@conference." + mServer;

	LL_INFOS("XMPP") << "joining " << client_muc_jid << llendl;

	sendPresence(LLXMPP_PRESENCE_AVAILABLE, mJid, client_muc_jid);

	mGroup_name_to_data[muc] = xmpp_group (muc, session_id, muc_jid, client_muc_jid);

	return true;
}

bool LLXMPPClient::getGroupData(const std::string& name, xmpp_group& group, bool del)
{
	std::string muc = sl_name_to_xmpp(name);
	group_name_to_data_map::iterator it = mGroup_name_to_data.find(muc);

	if (it == mGroup_name_to_data.end())
		return false;

	group = it->second;

	if (del)
		mGroup_name_to_data.erase(it);

	return true;
}


static std::string xml_escape(const std::string &xml)
{
	std::string out = xml;
	size_t pos;

	pos = 0;
	for (pos = out.find('<', pos);
		 pos != std::string::npos;
		 out.find('<'))
	{
		out.replace(pos, 1, "&lt;");
	}

	pos = 0;
	for (pos = out.find('>', pos);
		 pos != std::string::npos;
		 out.find('>'))
	{
		out.replace(pos, 1, "&gt;");
	}

	return out;
}


bool LLXMPPClient::say(const std::string& channel, const std::string& message)
{
	xmpp_group group;
	if (!getGroupData(channel, group))
    {
		LL_INFOS("XMPP") << "mGroup_name_to_data didn't have " << channel
						 << llendl;
		return false;
    }

	xmpp_stanza_t *message_stanza, *body, *text;
	message_stanza = xmpp_stanza_new (mCtx);
	xmpp_stanza_set_name (message_stanza, "message");
	xmpp_stanza_set_type (message_stanza, "groupchat");

	LL_INFOS("XMPP") << "say -- muc jid is:" << group.group_jid.c_str()
					 << llendl;
	xmpp_stanza_set_attribute (message_stanza, "to", group.group_jid.c_str());

	body = xmpp_stanza_new (mCtx);
	xmpp_stanza_set_name (body, "body");

	text = xmpp_stanza_new (mCtx);
	xmpp_stanza_set_text (text, xml_escape(message).c_str ());
	xmpp_stanza_add_child (body, text);
	xmpp_stanza_add_child (message_stanza, body);

	xmpp_send (mConn, message_stanza);
	xmpp_stanza_release (message_stanza);

	return true;
}


bool LLXMPPClient::leave(const std::string& channel)
{
	xmpp_group group;
	if (!getGroupData(channel, group, true))
    {
		LL_INFOS("XMPP") << "mGroup_name_to_data didn't have " << channel
						 << llendl;
		return false;
    }

	LL_INFOS("XMPP") << "leaving " << group.group_jid << llendl;

	sendPresence(LLXMPP_PRESENCE_UNAVAILABLE, group.group_jid);

	return true;
}


bool LLXMPPClient::check_for_inbound_chat()
{
	unsigned long timeout = 1; /* millisecond? */
	switch (mConnected) {
	case LLXMPP_DISCONNECTED:
		connect();
		break;
	case LLXMPP_CONNECTED:
		xmpp_run_once (mCtx, timeout);
		break;
	case LLXMPP_PENDING:
		gStatusBar->setXMPPStatus(llxmpp_status_text[mConnected], mServer);
		LL_INFOS("XMPP") << "check_for_inbound_chat: connection status still pending" << llendl;
		xmpp_run_once (mCtx, timeout);
		break;
	}	
	return true;
}


void LLXMPPClient::handleInboundChat(inbound_xmpp_chat *iic,
									 const LLAvatarNameCache::key& key,
									 const LLAvatarName& av_name)
{
	/* Don't tell our subclass about this bit of chat until we have
	   all the details.  Wait until we have the sender's agent-id. */

	LL_INFOS("XMPP") << "in LLXMPPClient::handleInboundChat on callback from LLAvatarNameCache" << llendl;

	iic->from_id = av_name.mAgentID;

	LL_INFOS("XMPP") << "dque iic, from=" << iic->from << llendl;
	LL_INFOS("XMPP") << "dque iic, from_id=" << iic->from_id << llendl;
	LL_INFOS("XMPP") << "dque iic, from nick=" << iic->nickname << llendl;
	LL_INFOS("XMPP") << "dque iic, recipient=" << iic->recipient << llendl;
	LL_INFOS("XMPP") << "dque iic, recipient_id="
					 << iic->recipient_id << llendl;
	LL_INFOS("XMPP") << "dque iic, utf8_text=" << iic->utf8_text << llendl;

	if (iic->from_id == LLUUID::null || av_name.mIsTemporaryName)
	{
		LL_INFOS("XMPP") << "null agent-id or dummy entry for '"
						 << iic->from << "'" << llendl;
	}
	else
	{
	}
	delete iic;
}

void LLXMPPClient::displayPresence(const LLUUID& recipient_id,
								   const roster_item& roster_i,
								   bool modify_existing)
{
}

void LLXMPPClient::setRosterAgentID(roster_item *roster_i,
									const LLAvatarNameCache::key& key,
									const LLAvatarName& av_name)
{
	LL_DEBUGS("XMPP") << "setRosterAgentID for " << key.asString()
					  << " to " << av_name.mAgentID << llendl;
	roster_i->agent_id = av_name.mAgentID;
}

void LLXMPPClient::handlePresence(char *room_jid, char *real_jid, char *affiliation, char *role, presence_info& pi, bool online)
{
	if (!real_jid) {
		LL_WARNS("XMPP") << "presence without real jid, that's not cool." << llendl;
		return;
	}

    /* The room name is found on the from jid. */
	char *room_name = xmpp_jid_node (mCtx, room_jid);
	char *room_nick = xmpp_jid_resource (mCtx, room_jid);
	char *real_nick = xmpp_jid_node(mCtx, real_jid);
	if (!room_nick)
		room_nick = real_nick;

	group_name_to_data_map::iterator it = mGroup_name_to_data.find (room_name);
	if (it == mGroup_name_to_data.end())
    {
		LL_INFOS("XMPP") << "    presence mGroup_name_to_data didn't have "
						 << room_name << llendl;
		return;
    }

	room_jid_to_roster_map::iterator it_roster = it->second.roster.find (room_jid);

	if (it_roster != it->second.roster.end())
	{
		it_roster->second.online = online;
		it_roster->second.presence = pi;
		displayPresence(it->second.sl_session, it_roster->second, false);
	}
	else
	{
		it->second.roster[room_jid] = roster_item(affiliation,
												  role,
												  real_jid,
												  room_nick,
												  pi,
												  online);

		// Look up the agent id for this presence
		LLAvatarName av_name;
		LLAvatarNameCache::key key(real_nick);
        
		if (LLAvatarNameCache::getKey(key, &av_name)) {
			LL_INFOS("XMPP") << "first try, got id for " << key.asString()
							 << " ... " << av_name.mAgentID << llendl;
			it->second.roster[room_jid].agent_id = av_name.mAgentID;
		} else {
			LL_INFOS("XMPP") << key.asString() << " not found in cache, scheduling a callback if we find it later" << llendl;
			LLAvatarNameCache::getKey(key,
									  boost::bind(&LLXMPPClient::setRosterAgentID,
												  this, &it->second.roster[room_jid], _1, _2));
		}
		displayPresence(it->second.sl_session, it->second.roster[room_jid], true);
	}
}


/***************************************************/

void LLXMPPClient::onConnect()
{
	mConnected = LLXMPP_CONNECTED;
	mRetryAttempt = 0;
	LL_INFOS("XMPP") << "connected" << llendl;
	gStatusBar->setXMPPStatus(llxmpp_status_text[mConnected], mServer);

	xmpp_handler_add (mConn, version_handler,
					  "jabber:iq:version", "iq", NULL, this);
	xmpp_handler_add (mConn, message_handler, NULL, "message", NULL, this);
	xmpp_handler_add (mConn, presence_handler, NULL, "presence", NULL, this);

	/* Send initial <presence/> so that we appear online to contacts */
	sendPresence(LLXMPP_PRESENCE_AVAILABLE);
}

void LLXMPPClient::onError(const std::string& message)
{
	mLastError = message;
}

// This is for server presence
void LLXMPPClient::sendPresence(llxmpp_presence_t presence)
{
	xmpp_stanza_t* pres;
	pres = xmpp_stanza_new (mCtx);
	xmpp_stanza_set_name (pres, "presence");

	if (presence) // AVAILABLE doesn't have a type
		xmpp_stanza_set_attribute (pres, "type", llxmpp_presence_text[presence]);

	xmpp_send (mConn, pres);
	xmpp_stanza_release (pres);
}

// This is for room presence
void LLXMPPClient::sendPresence(llxmpp_presence_t presence,
								const std::string& to)
{
	xmpp_stanza_t* pres;
	pres = xmpp_stanza_new (mCtx);
	xmpp_stanza_set_name (pres, "presence");

	xmpp_stanza_set_attribute (pres, "to", to.c_str());

	if (presence) // AVAILABLE doesn't have a type
		xmpp_stanza_set_attribute (pres, "type", llxmpp_presence_text[presence]);

	xmpp_send (mConn, pres);
	xmpp_stanza_release (pres);
}

// This is for room presence, too
void LLXMPPClient::sendPresence(llxmpp_presence_t presence,
								const std::string& from,
								const std::string& to)
{
	xmpp_stanza_t* pres;
	pres = xmpp_stanza_new (mCtx);
	xmpp_stanza_set_name (pres, "presence");

	xmpp_stanza_set_attribute (pres, "from", from.c_str());
	xmpp_stanza_set_attribute (pres, "to", to.c_str());

	if (presence) // AVAILABLE doesn't have a type
		xmpp_stanza_set_attribute (pres, "type", llxmpp_presence_text[presence]);

	xmpp_send (mConn, pres);
	xmpp_stanza_release (pres);
}

void LLXMPPClient::onDisconnect()
{
	xmpp_stop (mCtx);
	mConnected = LLXMPP_DISCONNECTED;
	++mRetryAttempt;
	LL_INFOS("XMPP") << "disconnected: " << llendl;
	gStatusBar->setXMPPStatus(llxmpp_status_text[mConnected], mServer);
	/*
	delete conn;
	conn = NULL;
	delete ctx;
	ctx = NULL;
	*/
}


void LLXMPPClient::handleGroupchatMessage(char *from_jid, char *subject, char *body)
{
	LL_INFOS("XMPP") << "Incoming groupchat message from " << from_jid
					 << " subject " << (subject ? subject : "(null subject)")
					 << " body " << (body ? body : "(null body)")
					 << llendl;
	/*
	  Incoming message from
	  the-lindens@conference.chat.aditi.lindenlab.com/seth.linden
	  okokok

	  For not-yet-understood reasons, sometimes a message is sent
	  with a jid format of 'group-name@chat-host' but no /nick part.
	  In this case, we'll use the chat room as the nick.
	  FIXME: There will probably be display name issues with this.
	*/

	char *room_name = xmpp_jid_node (mCtx, from_jid);
	char *from_nickname = xmpp_jid_resource (mCtx, from_jid);
	if (! from_nickname)
		from_nickname = (char *)"";

	LL_INFOS ("XMPP") << "  group name is: " << room_name << llendl;
	LL_INFOS ("XMPP") << "  sender jid is: " << from_jid << llendl;
	LL_INFOS ("XMPP") << "  sender nick is: " << from_nickname << llendl;

	group_name_to_data_map::iterator it = mGroup_name_to_data.find (room_name);
	if (it == mGroup_name_to_data.end())
    {
		LL_INFOS("XMPP") << "mGroup_name_to_data didn't have "
						 << room_name << llendl;
    }
	else
	{
		room_jid_to_roster_map::iterator it_roster = it->second.roster.find (from_jid);
		if (it_roster == it->second.roster.end())
		{
			LL_INFOS("XMPP") << "roster does not have presence for "
							 << from_jid << llendl;
		}
		else
		{
			roster_item roster_i = it_roster->second;

			char *from_username = xmpp_jid_node(mCtx, roster_i.jid.c_str());

			if (!from_username) {
			}

			if (body && from_username) {
				LL_INFOS("XMPP") << "from username is " << from_username << llendl;

				inbound_xmpp_chat iic = inbound_xmpp_chat(xmpp_name_to_sl(from_username),
														  roster_i.agent_id,
														  from_nickname,
														  xmpp_name_to_sl(room_name),
														  it->second.sl_session,
														  body);

				displayGroupchatMessage(iic);
			}

			if (subject && from_username) {
				displayGroupchatSubject(it->second.sl_session, from_username, subject);
			}

			xmpp_free (mCtx, from_username);
		}
	}
    
	xmpp_free (mCtx, room_name);
}


void LLXMPPClient::handleChatMessage(char *from_jid, char *subject, char *body)
{
	inbound_xmpp_chat *iic = NULL;

	// FIXME: super lame, refactor this.
	body = body ? body : (char *)"";
	subject = subject ? subject : (char *)"";
	LL_INFOS("XMPP") << "Incoming private message from " << from_jid
					 << " subject " << subject << " body " << body << llendl;

	char *sender_nick = xmpp_jid_resource (mCtx, from_jid);
	LL_INFOS ("XMPP") << "  sender nick is: " << sender_nick << llendl;

	iic = new inbound_xmpp_chat (sender_nick, "", "", LLUUID::null, body);

	if (iic->from.length() > 0)
		LLAvatarNameCache::getKey(iic->from,
								  boost::bind(&LLXMPPClient::handleInboundChat,
											  this, iic, _1, _2));
	else
		delete iic;
}

void LLXMPPClient::displayUnusualMessage(const std::string& type,
										 const std::string& subject,
										 const std::string& body)
{
}

void LLXMPPClient::displayGroupchatSubject(const LLUUID& session_id,
										   const std::string& who,
										   const std::string& subject)
{
}

void LLXMPPClient::displayGroupchatMessage(const inbound_xmpp_chat& iic)
{
}

void LLXMPPClient::displayGroupchatError(const inbound_xmpp_chat& iic)
{
}

void LLXMPPClient::handleHeadlineMessage(char *from_jid, char *subject, char *body)
{
	// FIXME: super lame, refactor this.
	body = body ? body : (char *)"";
	subject = subject ? subject : (char *)"";
	LL_INFOS("XMPP") << "Incoming headline message from " << from_jid
					 << " subject " << subject << " body " << body << llendl;

	displayUnusualMessage("headline", subject, body);
}

void LLXMPPClient::handleNormalMessage(char *from_jid, char *subject, char *body)
{
	// FIXME: super lame, refactor this.
	body = body ? body : (char *)"";
	subject = subject ? subject : (char *)"";
	LL_INFOS("XMPP") << "Incoming normal message from " << from_jid
					 << " subject " << subject << " body " << body << llendl;

	displayUnusualMessage("normal", subject, body);
}

/* errtype is one of: modify, cancel, auth, wait.
 * errword is one of:
 *   <bad-request/> 
 *   <conflict/> 
 *   <feature-not-implemented/> 
 *   <forbidden/> 
 *   <gone/> 
 *   <internal-server-error/> 
 *   <item-not-found/> 
 *   <jid-malformed/> 
 *   <not-acceptable/> 
 *   <not-allowed/> 
 *   <not-authorized/> 
 *   <payment-required/> 
 *   <recipient-unavailable/> 
 *   <redirect/> 
 *   <registration-required/> 
 *   <remote-server-not-found/> 
 *   <remote-server-timeout/> 
 *   <resource-constraint/> 
 *   <service-unavailable/> 
 *   <subscription-required/> 
 *   <undefined-condition/> 
 *   <unexpected-request/> 
 */
void LLXMPPClient::handleErrorMessage(char *from_jid, char *subject, char *body,
									  char *errtype, char *errword)
{
	// FIXME: super lame, refactor this.
	body = body ? body : (char *)"";
	subject = subject ? subject : (char *)"";
	errtype = errtype ? errtype : (char *)"";
	errword = errword ? errword : (char *)"";
	LL_INFOS("XMPP") << "Incoming error message from " << from_jid
					 << " subject " << subject
					 << " body " << body
					 << " errtype " << errtype
					 << " errword " << errword << llendl;

	char *room_name = xmpp_jid_node(mCtx, from_jid);
	group_name_to_data_map::iterator it = mGroup_name_to_data.find (room_name);
	if (it != mGroup_name_to_data.end())
    {
		inbound_xmpp_chat iic = inbound_xmpp_chat(xmpp_name_to_sl(room_name),
												  it->second.sl_session,
												  xmpp_name_to_sl(room_name),
												  xmpp_name_to_sl(room_name),
												  it->second.sl_session,
												  errword);

		displayGroupchatMessage(iic);
    }
	else
	{
		displayUnusualMessage("error", subject, body);
	}

	xmpp_free(mCtx, room_name);
}

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


