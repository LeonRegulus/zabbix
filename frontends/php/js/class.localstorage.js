/*
** Zabbix
** Copyright (C) 2001-2019 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

ZBX_LocalStorage.defines = {
	PREFIX_SEPARATOR: ':',
	KEEP_ALIVE_INTERVAL: 30,
	KEY_SESSIONS: 'sessions',
	EVT_CHANGE: 1,
	EVT_MAP: 2
}

/**
 * Local storage wrapper. Implements singleton.
 *
 * @param {string} version  Mandatory parameter.
 * @param {string} prefix  Used to distinct keys between sessions within same domain.
 */
function ZBX_LocalStorage(version, prefix) {
	if (!version || !prefix) {
		throw 'Local storage instantiation must be versioned, and prefixed.';
	}

	if (ZBX_LocalStorage.instance) {
		return ZBX_LocalStorage.instance;
	}
	ZBX_LocalStorage.sessionid = prefix;
	ZBX_LocalStorage.prefix = prefix + ZBX_LocalStorage.defines.PREFIX_SEPARATOR;
	ZBX_LocalStorage.instance = this;
	ZBX_LocalStorage.signature = (Math.random() % 9e6).toString(36).substr(2);

	this.keys = {
		// Store versioning.
		'version': version,
		// {(string) tabId: (int) lastSeen}
		// An object where every tab updates timestamp at key of it's id, in order to assume if there are crashed tabs.
		'tabs.lastseen': {},
		// Browser tab id that was the last one focused.
		'tabs.lastfocused': '',
		// Browser tab id that was the last one left focus.
		'tabs.lastblured': '',
		// Stores manifest for notifications that currently are in DOM. Keyed by id.
		'notifications.list': {},
		// {id: boolean} this is not a part of Notification, because it is not sent by server,
		// we must merge this property into list upon receive new / old notifications from server.
		'notifications.snoozedids': {},
		// When we receive list of messages, this signifies the time of latest message. This way we know if list is updated.
		'notifications.listid': '',
		// This setting will disable it self upon 'notifications.timestamp' update.
		'notifications.alarm.snoozed': '',
		// If this is true - notifications are still received, but no audio related things are ever performed.
		'notifications.alarm.muted': false,
		// If this value is not empty - that means it is playing. Seek and timeout is read from store on focus gain.
		'notifications.alarm.wave': '',
		// Optional seek position, it will always be read on focus, if playing.
		'notifications.alarm.seek': 0,
		// Set notifications audio player timeout.
		// Current timeout is written when playing tab lost focus or unloads.
		// Zero value is written along with notifications.alarm.start if (newValue != oldValue).
		'notifications.alarm.timeout': 0,
		// Notification start id is written when we receive a notification that should be played.
		'notifications.alarm.start': '',
		// Notification end id is written when notification has completed it's alert.
		// It is then checked if these keys are equal to know that we do not play notification again.
		'notifications.alarm.end': ''
	}

	// This subset of keys will be written also into session storage.
	// This way we surrvive data across page reloads in case of single tab.
	this.keysToBackup = {
		'notifications.alarm.end': true
	}

	if (this.readKey('version') != this.keys.version) {
		this.truncate();
	}

	this.keepAlive();
	setInterval(this.keepAlive, ZBX_LocalStorage.defines.KEEP_ALIVE_INTERVAL * 1000);
}

/**
 * Keeps alive local storage sessions.
 * Removes inactive session.
 */
ZBX_LocalStorage.prototype.keepAlive = function() {
	var timestamp = Math.floor(+new Date / 1000);
	var sessions = JSON.parse(localStorage.getItem(ZBX_LocalStorage.defines.KEY_SESSIONS) || '{}');

	var aliveIds = [];
	var expiredTimestamp = timestamp - 2 * ZBX_LocalStorage.defines.KEEP_ALIVE_INTERVAL;

	for (var id in sessions) {
		if (sessions[id] < expiredTimestamp) {
			delete sessions[id];
		}
		else {
			aliveIds.push(id);
		}
	}

	for (var i = 0; i < localStorage.length; i++) {
		var pts = localStorage.key(i).split(ZBX_LocalStorage.defines.PREFIX_SEPARATOR);
		if (pts.length < 2) {
			continue;
		}
		if (-1 === aliveIds.indexOf(pts[0])) {
			localStorage.removeItem(localStorage.key(i));
		}
	}

	sessions[ZBX_LocalStorage.sessionid] = timestamp;
	localStorage.setItem(ZBX_LocalStorage.defines.KEY_SESSIONS, ZBX_LocalStorage.stringify(sessions));
}

/**
 * Callback gets passed a reference of object under this key.
 * The reference then is written back into local storage.
 *
 * @param {string} key
 * @param {callable} callback
 */
ZBX_LocalStorage.prototype.mutateObject = function(key, callback) {
	var obj = this.readKey(key);
	callback(obj);
	this.writeKey(key, obj);
}

/**
 * Validates if key is used by this version of localStorage.
 *
 * @param {string} key  Key to test.
 *
 * @return {bool}
 */
ZBX_LocalStorage.prototype.hasKey = function(key) {
	return typeof this.keys[key] !== 'undefined';
}

/**
 * Alias to throw error on invalid key access.
 *
 * @param {string} key  Key to test.
 */
ZBX_LocalStorage.prototype.ensureKey = function(key) {
	if (typeof key !== 'string') {
		throw 'Key must be a string, ' + (typeof key) + ' given instead.';
	}

	if (!this.hasKey(key)) {
		throw 'Unknown localStorage key access at "'+key+'"';
	}
}

/**
 * Transforms absolute key into relative key.
 *
 * @param {string} absKey
 *
 * @return {string|null}  Relative key if found.
 */
ZBX_LocalStorage.prototype.fromAbsKey = function(absKey) {
	var match = absKey.match('^'+ZBX_LocalStorage.prefix+'(.*)');

	if (match !== null) {
		match = match[1];
	}

	return match;
}

/**
 * Transform key into absolute key.
 *
 * @param {string} key
 *
 * @return {string}
 */
ZBX_LocalStorage.prototype.toAbsKey = function(key) {
	return ZBX_LocalStorage.prefix+key;
}

/**
 * Writes an underlaying value.
 *
 * @param {string} key
 * @param {string} value
 */
ZBX_LocalStorage.prototype.writeKey = function(key, value) {
	if (typeof value === 'undefined') {
		throw 'Value may not be undefined, use null instead';
	}

	this.ensureKey(key);

	value = this.wrap(value);
	if (this.keysToBackup[key]) {
		sessionStorage.setItem(this.toAbsKey(key), value);
	}
	localStorage.setItem(this.toAbsKey(key), value);
}

/**
 * Writes default value.
 *
 * @param {string} key  Key to reset.
 */
ZBX_LocalStorage.prototype.resetKey = function(key) {
	this.ensureKey(key);
	this.writeKey(key, this.keys[key]);
}

/**
 * Fetches underlaying value.
 *
 * @param {string} key  Key to test.
 *
 * @return {mixed}
 */
ZBX_LocalStorage.prototype.readKey = function(key) {
	this.ensureKey(key);

	try {
		var absKey = this.toAbsKey(key);

		// A copy of default value is retured if key has no data.
		var item = localStorage.getItem(absKey)
			|| this.keysToBackup[key] && sessionStorage.getItem(absKey)
			|| this.wrap(this.keys[key]);

		return this.unwrap(item).payload;
	} catch (e) {
		console.warn('failed to parse storage item "'+key+'"');
		this.truncate();
		this.truncateBackup();
		return null;
	}
}

/**
 * This method exists because Prototype.js defines Array.prototype.toJSON method.
 * Which then is invoked by JSON.stringify producing unexpected result.
 * Since Prototype.js itself depends on it's implementation,
 * it is decided to not delete Array.prototype.toJSON.
 * Prototype.js provides Object.prototype.toJSON.
 *
 * @param {mixed} value
 *
 * @return {string} Valid JSON string.
 */
ZBX_LocalStorage.stringify = function(value) {
	return window.Prototype ? Object.toJSON(value) : JSON.stringify(value);
}

/**
 * @param {mixed} value
 *
 * @return {string}
 */
ZBX_LocalStorage.prototype.wrap = function(value) {
	return ZBX_LocalStorage.stringify({
		payload: value,
		signature: ZBX_LocalStorage.signature
	});
}

/**
 * @param {string} value
 *
 * @return {mixed}
 */
ZBX_LocalStorage.prototype.unwrap = function(value) {
	return JSON.parse(value);
}

/**
 * After this method call local storage will not perform any further writes.
 */
ZBX_LocalStorage.prototype.destruct = function() {
	this.writeKey = function() {}
	this.truncate();
	this.truncateBackup();
}

/**
 * Backup keys are removed.
 *
 * @param {string} value
 */
ZBX_LocalStorage.prototype.truncateBackup = function() {
	for (var i = 0; i < sessionStorage.length; i++) {
		var absKey = sessionStorage.key(i);
		var key = this.fromAbsKey(absKey);

		if (key && this.keysToBackup[key]) {
			sessionStorage.removeItem(absKey);
		}
	}
}

/**
 * Removes all local storage and creates default objects.
 * Backup keys are not removed.
 *
 * @param {string} value
 */
ZBX_LocalStorage.prototype.truncate = function() {
	this.iterator().forEach(function(keyVariants) {
		localStorage.removeItem(keyVariants.absKey);
	});
}

/**
 * Registers an event handler.
 * A callback will get passed key that were modified and the new value it now holds.
 * Note: handle is fired only when there was a change (not in case of any writeKey).
 *
 * @param {callable} callback
 */
ZBX_LocalStorage.prototype.onUpdate = function(callback) {
	window.addEventListener('storage', function(event) {
		// This key is for internal use only.
		if (event.key === ZBX_LocalStorage.defines.KEY_SESSIONS) {
			return;
		}

		// This means, storage has been truncated.
		if (event.key === null || event.key === '') {
			return this.mapCallback(callback);
		}

		// I do not know why this may happen, but it does.
		// Null cannot be accepted, because we should be able to unwrap the value.
		if (event.newValue === null) {
			return;
		}

		// Not only IE dispatches this event 'onwrite' instead of 'onchange',
		// but event is also dispatched in window that is the modifier.
		// So we need to sign all payloads.
		var value = this.unwrap(event.newValue);
		if (value.signature !== ZBX_LocalStorage.signature) {
			callback(this.fromAbsKey(event.key), value.payload, ZBX_LocalStorage.defines.EVT_CHANGE);
		}
	}.bind(this));
}

/**
 * Apply every callback for each localStorage entry.
 *
 * @param {callable} callback
 */
ZBX_LocalStorage.prototype.mapCallback = function(callback) {
	this.iterator().forEach(function(keyVariants) {
		if (this.hasKey(keyVariants.key)) {
			callback(keyVariants.key, this.readKey(keyVariants.key), ZBX_LocalStorage.defines.EVT_MAP);
		}
	}.bind(this));
}

/**
 * Returns list of keys found in localStorage relevant for current session.
 *
 * @return {array}
 */
ZBX_LocalStorage.prototype.iterator = function() {
	var length = localStorage.length;
	var list = [];

	for (var i = 0; i < length; i++) {
		var absKey = localStorage.key(i);
		var key = this.fromAbsKey(absKey);

		if (!key) {
			continue;
		}

		list.push({
			absKey: absKey,
			key: key
		});
	}

	return list;
}

ZABBIX.namespace(
	'instances.localStorage',
	new ZBX_LocalStorage('1', cookie.read('localstoragePath'))
);

