<?php
/*
** Zabbix
** Copyright (C) 2001-2015 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/


$discoveryRule = $data['discovery_rule'];
$hostPrototype = $data['host_prototype'];
$parentHost = $data['parent_host'];

require_once dirname(__FILE__).'/js/configuration.host.edit.js.php';
require_once dirname(__FILE__).'/js/configuration.host.prototype.edit.js.php';

$widget = (new CWidget())
	->setTitle(_('Host prototypes'))
	->addItem(get_header_host_table('hosts', $discoveryRule['hostid'], $discoveryRule['itemid']));

$divTabs = new CTabView();
if (!isset($_REQUEST['form_refresh'])) {
	$divTabs->setSelected(0);
}

$frmHost = (new CForm())
	->setName('hostPrototypeForm.')
	->addVar('form', getRequest('form', 1))
	->addVar('parent_discoveryid', $discoveryRule['itemid'])
	->addVar('tls_accept', $parentHost['tls_accept']);

$hostList = new CFormList('hostlist');

if ($hostPrototype['templateid'] && $data['parents']) {
	$parents = [];
	foreach (array_reverse($data['parents']) as $parent) {
		$parents[] = new CLink($parent['parentHost']['name'],
			'?form=update&hostid='.$parent['hostid'].'&parent_discoveryid='.$parent['discoveryRule']['itemid']
		);
		$parents[] = SPACE.'&rArr;'.SPACE;
	}
	array_pop($parents);
	$hostList->addRow(_('Parent discovery rules'), $parents);
}

if (isset($hostPrototype['hostid'])) {
	$frmHost->addVar('hostid', $hostPrototype['hostid']);
}

$hostTB = (new CTextBox('host', $hostPrototype['host'], (bool) $hostPrototype['templateid']))
	->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
	->setAttribute('maxlength', 64)
	->setAttribute('autofocus', 'autofocus');
$hostList->addRow(_('Host name'), $hostTB);

$name = ($hostPrototype['name'] != $hostPrototype['host']) ? $hostPrototype['name'] : '';
$visiblenameTB = (new CTextBox('name', $name, (bool) $hostPrototype['templateid']))
	->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
	->setAttribute('maxlength', 64);
$hostList->addRow(_('Visible name'), $visiblenameTB);

// display inherited parameters only for hosts prototypes on hosts
if ($parentHost['status'] != HOST_STATUS_TEMPLATE) {
	$existingInterfaceTypes = [];
	foreach ($parentHost['interfaces'] as $interface) {
		$existingInterfaceTypes[$interface['type']] = true;
	}

	zbx_add_post_js('hostInterfacesManager.add('.CJs::encodeJson(array_values($parentHost['interfaces'])).');');
	zbx_add_post_js('hostInterfacesManager.disable();');

	$ifTab = (new CTable())
		->setId('agentInterfaces')
		->setHeader([
			(new CColHeader())->setColSpan(2),
			_('IP address'),
			_('DNS name'),
			_('Connect to'),
			_('Port'),
			(new CColHeader(_('Default')))->setColSpan(2)
		]);

	// Zabbix agent interfaces
	$row = (new CRow(
		(new CCol(_('Agent')))
			->addClass('interface_name')
			->setRowSpan(1)
	))
		->setId('agentInterfacesFooter')
		->addClass('interface_type_'.INTERFACE_TYPE_AGENT);
	if (!array_key_exists(INTERFACE_TYPE_AGENT, $existingInterfaceTypes)) {
		$row
			->addItem(new CCol())
			->addItem((new CCol(_('No agent interfaces found.')))->setColSpan(6));
	}
	$ifTab->addRow($row)->addRow((new CCol())->setColSpan(8));

	// SNMP interfaces
	$row = (new CRow(
		(new CCol(_('SNMP')))
			->addClass('interface_name')
			->setRowSpan(1)
	))
		->setId('SNMPInterfacesFooter')
		->addClass('interface_type_'.INTERFACE_TYPE_SNMP);
	if (!array_key_exists(INTERFACE_TYPE_SNMP, $existingInterfaceTypes)) {
		$row
			->addItem(new CCol())
			->addItem((new CCol(_('No SNMP interfaces found.')))->setColSpan(6));
	}
	$ifTab->addRow($row)->addRow((new CCol())->setColSpan(8));

	// JMX interfaces
	$row = (new CRow(
		(new CCol(_('JMX')))
			->addClass('interface_name')
			->setRowSpan(1)
	))
		->setId('JMXInterfacesFooter')
		->addClass('interface_type_'.INTERFACE_TYPE_JMX);
	if (!array_key_exists(INTERFACE_TYPE_JMX, $existingInterfaceTypes)) {
		$row
			->addItem(new CCol())
			->addItem((new CCol(_('No JMX interfaces found.')))->setColSpan(6));
	}
	$ifTab->addRow($row)->addRow((new CCol())->setColSpan(8));

	// IPMI interfaces
	$row = (new CRow(
			(new CCol(_('IPMI')))
			->addClass('interface_name')
			->setRowSpan(1)
	))
		->setId('IPMIInterfacesFooter')
		->addClass('interface_type_'.INTERFACE_TYPE_IPMI);
	if (!array_key_exists(INTERFACE_TYPE_IPMI, $existingInterfaceTypes)) {
		$row
			->addItem(new CCol())
			->addItem((new CCol(_('No IPMI interfaces found.')))->setColSpan(6));
	}
	$ifTab->addRow($row);

	$hostList->addRow(_('Interfaces'),
		(new CDiv($ifTab))
			->addClass(ZBX_STYLE_TABLE_FORMS_SEPARATOR)
			->setAttribute('data-type', 'ipmi')
			->setAttribute('style', 'min-width:'.ZBX_HOST_INTERFACE_WIDTH.';')
	);

	// proxy
	$proxyTb = (new CTextBox('proxy_hostid',
		$parentHost['proxy_hostid'] != 0 ? $this->data['proxy']['host'] : _('(no proxy)'), true
	))
		->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH);
	$hostList->addRow(_('Monitored by proxy'), $proxyTb);
}

$hostList->addRow(_('Enabled'),
	(new CCheckBox('status', HOST_STATUS_MONITORED))
		->setChecked(HOST_STATUS_MONITORED == $hostPrototype['status'])
);

$divTabs->addTab('hostTab', _('Host'), $hostList);

// groups
$groupList = new CFormList();

// existing groups
$groups = [];
foreach ($data['groups'] as $group) {
	$groups[] = [
		'id' => $group['groupid'],
		'name' => $group['name']
	];
}
$groupList->addRow(_('Groups'),
	(new CMultiSelect([
		'name' => 'group_links[]',
		'objectName' => 'hostGroup',
		'objectOptions' => [
			'editable' => true,
			'filter' => ['flags' => ZBX_FLAG_DISCOVERY_NORMAL]
		],
		'data' => $groups,
		'disabled' => (bool) $hostPrototype['templateid'],
		'popup' => [
			'parameters' => 'srctbl=host_groups&dstfrm='.$frmHost->getName().'&dstfld1=group_links_'.
				'&srcfld1=groupid&writeonly=1&multiselect=1&normal_only=1'
		]
	]))->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
);

// new group prototypes
$customGroupTable = (new CTable())->setId('tbl_group_prototypes');

// buttons
$buttonColumn = (new CCol(
	(new CButton('group_prototype_add', _('Add')))->addClass(ZBX_STYLE_BTN_LINK)
))->setAttribute('colspan', 5);

$buttonRow = (new CRow())
	->setId('row_new_group_prototype')
	->addItem($buttonColumn);

$customGroupTable->addRow($buttonRow);
$groupList->addRow(_('Group prototypes'), (new CDiv($customGroupTable))->addClass(ZBX_STYLE_TABLE_FORMS_SEPARATOR));

$divTabs->addTab('groupTab', _('Groups'), $groupList);

// templates
$tmplList = new CFormList();

if ($hostPrototype['templateid']) {
	$linkedTemplateTable = (new CTable())
		->setNoDataMessage(_('No templates linked.'))
		->setAttribute('style', 'width: 100%;')
		->setHeader([_('Name')]);

	foreach ($hostPrototype['templates'] as $template) {
		$tmplList->addVar('templates['.$template['templateid'].']', $template['templateid']);
		$templateLink = (new CLink($template['name'], 'templates.php?form=update&templateid='.$template['templateid']))
			->setTarget('_blank');

		$linkedTemplateTable->addRow([$templateLink]);
	}

	$tmplList->addRow(_('Linked templates'),
		(new CDiv($linkedTemplateTable))
			->addClass(ZBX_STYLE_TABLE_FORMS_SEPARATOR)
			->setAttribute('style', 'min-width: '.ZBX_TEXTAREA_BIG_WIDTH.'px;')
	);
}
else {
	$ignoreTemplates = [];

	$linkedTemplateTable = (new CTable())
		->setNoDataMessage(_('No templates linked.'))
		->setAttribute('style', 'width: 100%;')
		->setHeader([_('Name'), _('Action')]);

	foreach ($hostPrototype['templates'] as $template) {
		$tmplList->addVar('templates['.$template['templateid'].']', $template['templateid']);
		$templateLink = (new CLink($template['name'], 'templates.php?form=update&templateid='.$template['templateid']))
			->setTarget('_blank');

		$linkedTemplateTable->addRow([
			$templateLink,
			(new CCol(
				(new CSubmit('unlink['.$template['templateid'].']', _('Unlink')))->addClass(ZBX_STYLE_BTN_LINK)
			))->addClass(ZBX_STYLE_NOWRAP)
		]);

		$ignoreTemplates[$template['templateid']] = $template['name'];
	}

	$tmplList->addRow(_('Linked templates'),
		(new CDiv($linkedTemplateTable))
			->addClass(ZBX_STYLE_TABLE_FORMS_SEPARATOR)
			->setAttribute('style', 'min-width: '.ZBX_TEXTAREA_BIG_WIDTH.'px;')
	);

	// create new linked template table
	$newTemplateTable = (new CTable())
		->addRow([
			(new CMultiSelect([
				'name' => 'add_templates[]',
				'objectName' => 'templates',
				'ignored' => $ignoreTemplates,
				'popup' => [
					'parameters' => 'srctbl=templates&srcfld1=hostid&srcfld2=host&dstfrm='.$frmHost->getName().
						'&dstfld1=add_templates_&templated_hosts=1&multiselect=1'
				]
			]))->setWidth(ZBX_TEXTAREA_STANDARD_WIDTH)
		])
		->addRow([(new CSubmit('add_template', _('Add')))->addClass(ZBX_STYLE_BTN_LINK)]);

	$tmplList->addRow(_('Link new templates'),
		(new CDiv($newTemplateTable))
			->addClass(ZBX_STYLE_TABLE_FORMS_SEPARATOR)
			->setAttribute('style', 'min-width: '.ZBX_TEXTAREA_BIG_WIDTH.'px;')
	);
}

$divTabs->addTab('templateTab', _('Templates'), $tmplList);

// display inherited parameters only for hosts prototypes on hosts
if ($parentHost['status'] != HOST_STATUS_TEMPLATE) {
	// IPMI
	$ipmiList = new CFormList();

	$ipmiList->addRow(_('Authentication algorithm'),
		(new CTextBox('ipmi_authtype', ipmiAuthTypes($parentHost['ipmi_authtype']), true))
			->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
	);
	$ipmiList->addRow(_('Privilege level'),
		(new CTextBox('ipmi_privilege', ipmiPrivileges($parentHost['ipmi_privilege']), true))
			->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
	);
	$ipmiList->addRow(_('Username'),
		(new CTextBox('ipmi_username', $parentHost['ipmi_username'], true))->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
	);
	$ipmiList->addRow(_('Password'),
		(new CTextBox('ipmi_password', $parentHost['ipmi_password'], true))->setWidth(ZBX_TEXTAREA_SMALL_WIDTH)
	);

	$divTabs->addTab('ipmiTab', _('IPMI'), $ipmiList);

	// macros
	$macros = $parentHost['macros'];
	if ($data['show_inherited_macros']) {
		$macros = mergeInheritedMacros($macros,
			getInheritedMacros(zbx_objectValues($hostPrototype['templates'], 'templateid'))
		);
	}
	$macros = array_values(order_macros($macros, 'macro'));

	$macrosView = new CView('hostmacros', [
		'macros' => $macros,
		'show_inherited_macros' => $data['show_inherited_macros'],
		'is_template' => false,
		'readonly' => true
	]);
	$divTabs->addTab('macroTab', _('Macros'), $macrosView->render());
}

$inventoryFormList = (new CFormList('inventorylist'))
	->addRow(null,
		(new CRadioButtonList('inventory_mode', (int) $hostPrototype['inventory']['inventory_mode']))
			->addValue(_('Disabled'), HOST_INVENTORY_DISABLED)
			->addValue(_('Manual'), HOST_INVENTORY_MANUAL)
			->addValue(_('Automatic'), HOST_INVENTORY_AUTOMATIC)
			->setEnabled($hostPrototype['templateid'] == 0)
			->setModern(true)
	);

$divTabs->addTab('inventoryTab', _('Host inventory'), $inventoryFormList);

// Encryption form list.
$encryption_form_list = (new CFormList('encryption'))
	->addRow(_('Connections to host'),
		(new CRadioButtonList('tls_connect', (int) $parentHost['tls_connect']))
			->addValue(_('No encryption'), HOST_ENCRYPTION_NONE)
			->addValue(_('PSK'), HOST_ENCRYPTION_PSK)
			->addValue(_('Certificate'), HOST_ENCRYPTION_CERTIFICATE)
			->setModern(true)
			->setEnabled(false)
	)
	->addRow(_('Connections from host'), [
		[(new CCheckBox('tls_in_none'))->setAttribute('disabled', 'disabled'), _('No encryption')],
		BR(),
		[(new CCheckBox('tls_in_psk'))->setAttribute('disabled', 'disabled'), _('PSK')],
		BR(),
		[(new CCheckBox('tls_in_cert'))->setAttribute('disabled', 'disabled'), _('Certificate')]
	])
	->addRow(_('PSK identity'),
		(new CTextBox('tls_psk_identity', $parentHost['tls_psk_identity'], false, 128))
			->setWidth(ZBX_TEXTAREA_BIG_WIDTH)
			->setAttribute('disabled', 'disabled')
	)
	->addRow(_('PSK'),
		(new CTextBox('tls_psk', $parentHost['tls_psk'], false, 512))
			->setWidth(ZBX_TEXTAREA_BIG_WIDTH)
			->setAttribute('disabled', 'disabled')
	)
	->addRow(_('Issuer'),
		(new CTextBox('tls_issuer', $parentHost['tls_issuer'], false, 1024))
			->setWidth(ZBX_TEXTAREA_BIG_WIDTH)
			->setAttribute('disabled', 'disabled')
	)
	->addRow(_('Subject'),
		(new CTextBox('tls_subject', $parentHost['tls_subject'], false, 1024))
			->setWidth(ZBX_TEXTAREA_BIG_WIDTH)
			->setAttribute('disabled', 'disabled')
	);

$divTabs->addTab('encryptionTab', _('Encryption'), $encryption_form_list);

/*
 * footer
 */
if (isset($hostPrototype['hostid'])) {
	$btnDelete = new CButtonDelete(
		_('Delete selected host prototype?'),
		url_param('form').url_param('hostid').url_param('parent_discoveryid')
	);
	$btnDelete->setEnabled($hostPrototype['templateid'] == 0);

	$divTabs->setFooter(makeFormFooter(
		new CSubmit('update', _('Update')),
		[
			new CSubmit('clone', _('Clone')),
			$btnDelete,
			new CButtonCancel(url_param('parent_discoveryid'))
		]
	));
}
else {
	$divTabs->setFooter(makeFormFooter(
		new CSubmit('add', _('Add')),
		[new CButtonCancel(url_param('parent_discoveryid'))]
	));
}

$frmHost->addItem($divTabs);
$widget->addItem($frmHost);

return $widget;
