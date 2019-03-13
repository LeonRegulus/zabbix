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


/**
 * This object is representing a browser tab. Implements singleton pattern.
 * It ensures there are only non-crashed tabs in store.
 * It maintains currently focused tab and last focused tab in store.
 *
 * @param store ZBX_LocalStorage  A localStorage wrapper.
 */
function ZBX_BrowserTab(store) {
	if (!(store instanceof ZBX_LocalStorage)) {
		throw 'Unmatched signature!';
	}

	if (ZBX_BrowserTab.instance) {
		return ZBX_BrowserTab.instance;
	}

	ZBX_BrowserTab.instance = this;
	this.uid = (Math.random() % 9e6).toString(36).substr(2);
	this.focused = false;
	this.store = store;
	this.keepAliveInterval = 30000; // 30 seconds

	this.onFocusCbs = [];
	this.onBlurCbs = [];
	this.onUnloadCbs = [];

	this.register();
}

/**
 * Gives all tab ids.
 *
 * @return array
 */
ZBX_BrowserTab.prototype.getAllTabIds = function() {
	return Object.keys(this.store.readKey('tabs.lastseen'));
}

/**
 * Looks for crashed tabs.
 */
ZBX_BrowserTab.prototype.checkAlive = function() {
	var now = Math.floor(+new Date / 1000) - 60;
	var tabs = this.store.readKey('tabs.lastseen');

	for (var tabId in tabs) {
		if (tabs[tabId] < now) {
			this.handleCrashed(tabId);
		}
	}
}

/**
 * @param string tabId  The crashed tab id.
 */
ZBX_BrowserTab.prototype.handleCrashed = function(tabId) {
	this.store.mutateObject('tabs.lastseen', function(tabs) {
		delete tabs[tabId];
	}.bind(this));

	if (this.store.readKey('tabs.lastfocused') == tabId) {
		this.store.writeKey('tabs.lastfocused', this.uid);
		console.info('Recovered a crashed tab '+tabId+'. Now tab ' + this.uid + ' is polling for notifications.');
		this.handleFocus();
	}
}

/**
 * Bind cb
 */
ZBX_BrowserTab.prototype.onBlur = function(callback) {
	this.onBlurCbs.push(callback);
}

/**
 * Bind cb
 */
ZBX_BrowserTab.prototype.onFocus = function(callback) {
	this.onFocusCbs.push(callback);
}

/**
 * Bind cb
 */
ZBX_BrowserTab.prototype.onUnload = function(callback) {
	this.onUnloadCbs.push(callback);
}

/**
 * Rewrite focused tab id.
 */
ZBX_BrowserTab.prototype.handleBlur = function(event) {
	this.onBlurCbs.forEach(function(c) {c(this)});
	this.store.writeKey('tabs.lastblured', this.uid);
}

/**
 * Rewrite focused tab id.
 */
ZBX_BrowserTab.prototype.handleFocus = function(event) {
	this.onFocusCbs.forEach(function(c) {c(this)}.bind(this));
	this.store.writeKey('tabs.lastfocused', this.uid);
}

/**
 * Delegate active tab to any alive tab and clean up localStorage.
 */
ZBX_BrowserTab.prototype.handleBeforeUnload = function(event) {
	var uid = this.uid;
	this.checkAlive();
	this.store.mutateObject('tabs.lastseen', function(tabs) {
		delete tabs[uid];
	});
	var allTabIds = this.getAllTabIds();
	this.store.writeKey('tabs.lastfocused', allTabIds.length ? allTabIds[0] : '');

	// This prop needs to be freed earlier, because it seems scripts do continue execution after this call.
	// Without this line it seems that focus event fired, thus it writes this tab id again back into store.
	delete this.store;

	this.onUnloadCbs.forEach(function(c) {c(this)}.bind(this));
}

/**
 * Compares instance with active ref from localStorage.
 *
 * @return bool
 */
ZBX_BrowserTab.prototype.isFocused = function() {
	return this.store.readKey('tabs.lastfocused') === this.uid;
}

/**
 * Determines if there are more sibling tabs.
 *
 * @return bool
 */
ZBX_BrowserTab.prototype.isSingleSession = function() {
	return this.getAllTabIds().length < 2;
}

/**
 * Updates timestamp for own ID in `store.tabs` object.
 */
ZBX_BrowserTab.prototype.keepAlive = function() {
	var uid = this.uid;
	this.store.mutateObject('tabs.lastseen', function(tabs) {
		tabs[uid] = Math.floor(+new Date / 1000);
	});
}

/**
 * Writes own ID in `store.tabs` object.
 * Registers beforeunload event to remove own ID from `store.tabs`.
 * Registers focus and blur events to maintain `store.tabs.lastfocused`.
 * Begins a loop to see if any tab of tabs has crashed.
 * Ensures that in case if tab chashed, unload event call is deligated.
 *
 * TODO fix caveat when focus event is not fired if a link to SINGLE-FIRST zabbix session is opened in background.
 */
ZBX_BrowserTab.prototype.register = function() {
	this.keepAlive();
	this.checkAlive();

	// every 30 seconds write client time for this tabId
	setInterval(function() {
		this.keepAlive();
		this.checkAlive();
	}.bind(this), this.keepAliveInterval);

	window.addEventListener('beforeunload', this.handleBeforeUnload.bind(this));
	window.addEventListener('focus', this.handleFocus.bind(this));
	window.addEventListener('blur', this.handleBlur.bind(this));
	if (document.hasFocus()) {
		this.handleFocus();
	}
}

