<?php
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


class CControllerNotificationsGet extends CController {

	protected function checkInput() {
		$fields = [
			'old_srv_time' => 'required|int32'
		];

		return $this->validateInput($fields);
	}

	protected function checkPermissions() {
		return (!CWebUser::isGuest() && $this->getUserType() >= USER_TYPE_ZABBIX_USER);
	}

	protected function doAction() {
		$msg_settings = getMessageSettings();

		$trigger_limit = 15;

		$result = [
			'notifications' => [],
			'listid' => '',
			'srv_time' => time(),
			'settings' => [
				'enabled' => (bool) $msg_settings['enabled'],
				'alarm_timeout' => intval($msg_settings['sounds.repeat']),
				'msg_timeout' => intval($msg_settings['timeout']),
				'muted' => (bool) $msg_settings['sounds.mute'],
				'files' => [
					'-1' => $msg_settings['sounds.recovery'],
					'0' => $msg_settings['sounds.0'],
					'1' => $msg_settings['sounds.1'],
					'2' => $msg_settings['sounds.2'],
					'3' => $msg_settings['sounds.3'],
					'4' => $msg_settings['sounds.4'],
					'5' => $msg_settings['sounds.5']
				]
			]
		];

		if (!$msg_settings['triggers.severities'] || !$msg_settings['enabled']) {
			return $this->setResponse(new CControllerResponseData(['main_block' => json_encode($result)]));
		}

		$options = [
			'monitored' => true,
			'lastChangeSince' => max([$msg_settings['last.clock'], time() - 2 * $msg_settings['timeout']]),
			'value' => [TRIGGER_VALUE_TRUE, TRIGGER_VALUE_FALSE],
			'priority' => array_keys($msg_settings['triggers.severities']),
			'triggerLimit' => $trigger_limit
		];
		if (!$msg_settings['triggers.recovery']) {
			$options['value'] = [TRIGGER_VALUE_TRUE];
		}

		$events = getLastEvents($options);

		$sort_clock = [];
		$sort_event = [];
		$used_triggers = [];

		foreach ($events as $event) {

			if ($this->input['old_srv_time']) {
				$poll_interval = time() - $this->input['old_srv_time'];
				$ttl_delta = $poll_interval - ((time() - $event['clock']) % $poll_interval);
			}
			else {
				$ttl_delta = $msg_settings['timeout'];
			}

			$ttl = $ttl_delta + $event['clock'] + $msg_settings['timeout'] - time();
			if ($ttl < 0) {
				continue;
			}

			if (count($used_triggers) == $trigger_limit) {
				break;
			}

			if (isset($used_triggers[$event['objectid']])) {
				continue;
			}

			$uid = $event['eventid'].'_'.$event['value'];
			$result['listid'] .= $uid;

			$trigger = $event['trigger'];
			$host = $event['host'];

			if ($event['value'] == TRIGGER_VALUE_FALSE) {
				$priority = 0;
				$title = _('Resolved');
				$fileid = '-1';
			}
			else {
				$priority = $trigger['priority'];
				$title = _('Problem on');
				$fileid = $trigger['priority'];
			}

			$url_tr_status = 'tr_status.php?hostid='.$host['hostid'];
			$url_events = 'events.php?filter_set=1&triggerid='.$event['objectid'].'&source='.EVENT_SOURCE_TRIGGERS;
			$url_tr_events = 'tr_events.php?eventid='.$event['eventid'].'&triggerid='.$event['objectid'];

			$result['notifications'][] = [
				'uid' => $uid,
				'id' => $event['eventid'],
				'ttl' => $ttl,
				'priority' => $priority,
				'file' => $fileid,
				'severity_style' => getSeverityStyle($trigger['priority'], $event['value'] == TRIGGER_VALUE_TRUE),
				'title' => $title.' [url='.$url_tr_status.']'.CHtml::encode($host['name']).'[/url]',
				'body' => [
					'[url='.$url_events.']'.CHtml::encode($trigger['description']).'[/url]',
					'[url='.$url_tr_events.']'.
						zbx_date2str(DATE_TIME_FORMAT_SECONDS, $event['clock']).'[/url]',
				]
			];

			$sort_clock[$uid] = $event['clock'];
			$sort_event[$uid] = $event['eventid'];
			$used_triggers[$event['objectid']] = true;
		}

		array_multisort($sort_clock, SORT_ASC, $sort_event, SORT_ASC, $result['notifications']);
		$result['listid'] = sprintf('%u', crc32($result['listid']));

		$this->setResponse(new CControllerResponseData(['main_block' => json_encode($result)]));
	}
}
