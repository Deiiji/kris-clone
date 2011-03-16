/** 
 * @file llavatarnamecache.h
 * @brief Provides lookup of avatar SLIDs ("bobsmith123") and display names
 * ("James Cook") from avatar UUIDs.
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

#ifndef LLAVATARNAMECACHE_H
#define LLAVATARNAMECACHE_H

#include "llavatarname.h"	// for convenience

#include <boost/signals2.hpp>

class LLSD;
class LLUUID;

namespace LLAvatarNameCache
{
	class key
	{
	public:
		enum kind_t {
			agent_id,
			username,
		};

		key(const std::string& name) : mKind(username), mKey(name) { }
		key(const LLUUID& id) : mKind(agent_id), mKey(id.asString()) { }
		key(const key& k) : mKind(k.mKind), mKey(k.mKey) { }
		const kind_t kind() const { return mKind; }

		const std::string& asString() const { return mKey; }
		LLUUID asUUID() const {
			if (mKind == agent_id)
				return LLUUID(mKey);
			return LLUUID::null;
		}

		const std::string& param() const {
			static const std::string ids("ids=");
			static const std::string user("username=");
			switch (mKind) {
			case username:
				return user;
			case agent_id:
			default:
				return ids;
			}
		}

		bool operator==(const key& b) const {
			return mKind == b.mKind && mKey == b.mKey;
		}

		bool operator<(const key& b) const {
			if (mKind == b.mKind)
				return mKey < b.mKey;
			return mKind < b.mKind;
		}
		
	private:
		std::string mKey;
		kind_t mKind;
	};
		
	typedef boost::signals2::signal<void (void)> use_display_name_signal_t;

	// Until the cache is set running, immediate lookups will fail and
	// async lookups will be queued.  This allows us to block requests
	// until we know if the first region supports display names.
	void initClass(bool running);
	void cleanupClass();

	void importFile(std::istream& istr);
	void exportFile(std::ostream& ostr);

	// On the viewer, usually a simulator capabilitity
	// If empty, name cache will fall back to using legacy name
	// lookup system
	void setNameLookupURL(const std::string& name_lookup_url);

	// Do we have a valid lookup URL, hence are we trying to use the
	// new display name lookup system?
	bool hasNameLookupURL();
	
	// Periodically makes a batch request for display names not already in
	// cache.  Call once per frame.
	void idle();

	// If name is in cache, returns true and fills in provided LLAvatarName
	// otherwise returns false
	bool get(const LLUUID& agent_id, LLAvatarName *av_name);
	bool getKey(const key& key, LLAvatarName *av_name);

	// Callback types for get() below
	typedef boost::signals2::signal<
		void (const LLUUID& agent_id, const LLAvatarName& av_name)>
			id_callback_signal_t;
	typedef id_callback_signal_t::slot_type id_callback_slot_t;
	typedef boost::signals2::signal<
		void (const key& key, const LLAvatarName& av_name)>
			callback_signal_t;
	typedef callback_signal_t::slot_type callback_slot_t;

	// Fetches name information and calls callback.
	// If name information is in cache, callback will be called immediately.
	void get(const LLUUID& agent_id, id_callback_slot_t slot);
	void getKey(const key& key, callback_slot_t slot);

	// Allow display names to be explicitly disabled for testing.
	void setUseDisplayNames(bool use);
	bool useDisplayNames();

	void erase(const LLUUID& agent_id);

    /// Provide some fallback for agents that return errors
	void handleAgentError(const key& key);

	// Force a re-fetch of the most recent data, but keep the current
	// data in cache
	void fetch(const LLUUID& agent_id);

	void insert(const LLUUID& agent_id, const LLAvatarName& av_name);

	// Compute name expiration time from HTTP Cache-Control header,
	// or return default value, in seconds from epoch.
	F64 nameExpirationFromHeaders(LLSD headers);

	void addUseDisplayNamesCallback(const use_display_name_signal_t::slot_type& cb);
}

// Parse a cache-control header to get the max-age delta-seconds.
// Returns true if header has max-age param and it parses correctly.
// Exported here to ease unit testing.
bool max_age_from_cache_control(const std::string& cache_control, S32 *max_age);

#endif
