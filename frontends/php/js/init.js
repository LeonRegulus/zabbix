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


jQuery(function($) {
	var App = {
		localStorage: null,
		browserTab: null,
		notifications: null,
		logout: function() {
			this.localStorage.truncate();

			var url = new Curl('index.php', true);
			url.setArgument('reconnect', 1);
			window.location.replace(url.getUrl());
		},
		construct: function() {
			this.localStorage = new ZBX_LocalStorage('1');
			this.browserTab = new ZBX_BrowserTab(this.localStorage);
			this.notifications = new ZBX_Notifications(this.localStorage, this.browserTab);
		},
		init: function() {
			document.body.appendChild(App.notifications.dom.node);
			$(App.notifications.dom.node).draggable();
		}
	}
	App.construct();
	App.init();
	window.App = App

	if ($('#search').length) {
		createSuggest('search');
	}

	if (IE) {
		setTimeout(function () { $('[autofocus]').focus(); }, 10);
	}

	/**
	 * Change combobox color according selected option.
	 */
	$('select').each(function() {
		var comboBox = $(this),
			changeClass = function(obj) {
				if (obj.find('option.red:selected').length > 0) {
					obj.addClass('red');
				}
				else {
					obj.removeClass('red');
				}
			};

		comboBox.change(function() {
			changeClass($(this));
		});

		changeClass(comboBox);
	});

	/**
	 * Build menu popup for given elements.
	 */
	$(document).on('click', '[data-menu-popup]', function(event) {
		var obj = $(this),
			data = obj.data('menu-popup');

		switch (data.type) {
			case 'favouriteGraphs':
				data = getMenuPopupFavouriteGraphs(data);
				break;

			case 'favouriteMaps':
				data = getMenuPopupFavouriteMaps(data);
				break;

			case 'favouriteScreens':
				data = getMenuPopupFavouriteScreens(data);
				break;

			case 'history':
				data = getMenuPopupHistory(data);
				break;

			case 'host':
				data = getMenuPopupHost(data);
				break;

			case 'map':
				data = getMenuPopupMap(data);
				break;

			case 'refresh':
				data = getMenuPopupRefresh(data);
				break;

			case 'trigger':
				data = getMenuPopupTrigger(data);
				break;

			case 'triggerLog':
				data = getMenuPopupTriggerLog(data);
				break;

			case 'triggerMacro':
				data = getMenuPopupTriggerMacro(data);
				break;
		}

		obj.menuPopup(data, event);

		return false;
	});

	/*
	 * add.popup event
	 *
	 * Call multiselect method 'addData' if parent was multiselect, execute addPopupValues function
	 * or just update input field value
	 *
	 * @param object data
	 * @param string data.object   object name
	 * @param array  data.values   values
	 * @param string data.parentId parent id
	 */
	$(document).on('add.popup', function(e, data) {
		// multiselect check
		if (data.parentId && $('#' + data.parentId).hasClass('multiselect')) {
			for (var i = 0; i < data.values.length; i++) {
				if (typeof data.values[i].id !== 'undefined') {
					var item = {
						'id': data.values[i].id,
						'name': data.values[i].name
					};

					if (typeof(data.values[i].prefix) !== 'undefined') {
						item.prefix = data.values[i].prefix;
					}

					jQuery('#' + data.parentId).multiSelect('addData', item);
				}
			}
		}
		else if (typeof addPopupValues !== 'undefined') {
			// execute function if they exist
			addPopupValues(data);
		}
		else if (data.parentId) {
			jQuery('#' + data.parentId).val(data.values[0].name);
		}

		if (data.elements) {
			jQuery.each(data.elements, function(_, element) {
				jQuery('#' + element.id).val(element.value);
			});
		}
	});

	// redirect buttons
	$('button[data-url]').click(function() {
		var button = $(this);
		var confirmation = button.data('confirmation');

		if (typeof confirmation === 'undefined' || (typeof confirmation !== 'undefined' && confirm(confirmation))) {
			window.location = button.data('url');
		}
	})
});
