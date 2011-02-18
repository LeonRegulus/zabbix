/*
** ZABBIX
** Copyright (C) 2000-2005 SIA Zabbix
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
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**/

#include "common.h"

#include "db.h"
#include "log.h"
#include "sysinfo.h"
#include "zbxicmpping.h"
#include "discovery.h"
#include "zbxserver.h"
#include "zbxself.h"

#include "daemon.h"
#include "discoverer.h"
#include "../poller/checks_agent.h"
#include "../poller/checks_snmp.h"

extern int		CONFIG_DISCOVERER_FORKS;
static unsigned char	zbx_process;
static unsigned char	process_type;
extern int		process_num;

/******************************************************************************
 *                                                                            *
 * Function: proxy_update_service                                             *
 *                                                                            *
 * Purpose: process new service status                                        *
 *                                                                            *
 * Parameters: service - service info                                         *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Aleksander Vladishev                                               *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static void proxy_update_service(DB_DRULE *drule, DB_DCHECK *dcheck, char *ip, int port, int status, const char *value, int now)
{
	char	*ip_esc, *key_esc, *value_esc;

	ip_esc = DBdyn_escape_string_len(ip, PROXY_DHISTORY_IP_LEN);
	key_esc = DBdyn_escape_string_len(dcheck->key_, PROXY_DHISTORY_KEY_LEN);
	value_esc = DBdyn_escape_string_len(value, PROXY_DHISTORY_VALUE_LEN);

	DBexecute("insert into proxy_dhistory (clock,druleid,dcheckid,type,ip,port,key_,value,status)"
			" values (%d," ZBX_FS_UI64 "," ZBX_FS_UI64 ",%d,'%s',%d,'%s','%s',%d)",
			now,
			drule->druleid,
			dcheck->dcheckid,
			dcheck->type,
			ip_esc,
			port,
			key_esc,
			value_esc,
			status);

	zbx_free(value_esc);
	zbx_free(key_esc);
	zbx_free(ip_esc);
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_update_host                                                *
 *                                                                            *
 * Purpose: process new service status                                        *
 *                                                                            *
 * Parameters: service - service info                                         *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Aleksander Vladishev                                               *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static void proxy_update_host(DB_DRULE *drule, char *ip, int status, int now)
{
	char	*ip_esc;

	ip_esc = DBdyn_escape_string_len(ip, PROXY_DHISTORY_IP_LEN);

	DBexecute("insert into proxy_dhistory (clock,druleid,type,ip,status)"
			" values (%d," ZBX_FS_UI64 ",-1,'%s',%d)",
			now,
			drule->druleid,
			ip_esc,
			status);

	zbx_free(ip_esc);
}

/******************************************************************************
 *                                                                            *
 * Function: discover_service                                                 *
 *                                                                            *
 * Purpose: check if service is available and update database                 *
 *                                                                            *
 * Parameters: service type, ip address, port number                          *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static int	discover_service(DB_DCHECK *dcheck, char *ip, int port, char *value)
{
	const char	*__function_name = "discover_service";
	int		ret = SUCCEED;
	char		key[MAX_STRING_LEN], error[ITEM_ERROR_LEN_MAX];
	const char	*service = NULL;
	AGENT_RESULT 	result;
	DC_ITEM		item;
	ZBX_FPING_HOST	host;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	init_result(&result);
	*value = '\0';

	switch (dcheck->type)
	{
		case SVC_SSH:	service = "ssh"; break;
		case SVC_LDAP:	service = "ldap"; break;
		case SVC_SMTP:	service = "smtp"; break;
		case SVC_FTP:	service = "ftp"; break;
		case SVC_HTTP:	service = "http"; break;
		case SVC_POP:	service = "pop"; break;
		case SVC_NNTP:	service = "nntp"; break;
		case SVC_IMAP:	service = "imap"; break;
		case SVC_TCP:	service = "tcp"; break;
		case SVC_AGENT:
		case SVC_SNMPv1:
		case SVC_SNMPv2c:
		case SVC_SNMPv3:
		case SVC_ICMPPING:
			break;
		default:
			ret = FAIL;
			break;
	}

	if (ret == SUCCEED)
	{
		alarm(10);

		switch(dcheck->type)
		{
			/* Simple checks */
			case SVC_SSH:
			case SVC_LDAP:
			case SVC_SMTP:
			case SVC_FTP:
			case SVC_HTTP:
			case SVC_POP:
			case SVC_NNTP:
			case SVC_IMAP:
			case SVC_TCP:
				zbx_snprintf(key, sizeof(key), "net.tcp.service[%s,%s,%d]", service, ip, port);

				if (SUCCEED == process(key, 0, &result))
				{
					if (GET_UI64_RESULT(&result))
					{
						if (result.ui64 == 0)
							ret = FAIL;
					}
					else
						ret = FAIL;
				}
				else
					ret = FAIL;
				break;
			/* Agent and SNMP checks */
			case SVC_AGENT:
			case SVC_SNMPv1:
			case SVC_SNMPv2c:
			case SVC_SNMPv3:
				memset(&item, 0, sizeof(DC_ITEM));
				zbx_strlcpy(item.key_orig, dcheck->key_, sizeof(item.key_orig));
				item.key = item.key_orig;
				zbx_strlcpy(item.host.ip, ip, sizeof(item.host.ip));
				item.host.useip	= 1;
				item.host.port	= port;

				item.value_type	= ITEM_VALUE_TYPE_STR;

				switch (dcheck->type)
				{
					case SVC_SNMPv1:
						item.type = ITEM_TYPE_SNMPv1;
						break;
					case SVC_SNMPv2c:
						item.type = ITEM_TYPE_SNMPv2c;
						break;
					case SVC_SNMPv3:
						item.type = ITEM_TYPE_SNMPv3;
						break;
					default:
						item.type = ITEM_TYPE_ZABBIX;
				}

				if (dcheck->type == SVC_AGENT)
				{
					if(SUCCEED == get_value_agent(&item, &result))
					{
						if (GET_STR_RESULT(&result))
							zbx_strlcpy(value, result.str, DSERVICE_VALUE_LEN_MAX);
						else
							ret = FAIL;
					}
					else
						ret = FAIL;
				}
				else
#ifdef HAVE_SNMP
				{
					item.snmp_port = port;
					item.snmp_community = strdup(dcheck->snmp_community);
					item.snmp_oid = strdup(dcheck->key_);

					substitute_simple_macros(NULL, NULL, NULL, NULL, NULL,
							&item.snmp_community, MACRO_TYPE_ITEM_FIELD, NULL, 0);
					substitute_simple_macros(NULL, NULL, NULL, NULL, NULL,
							&item.snmp_oid, MACRO_TYPE_ITEM_FIELD, NULL, 0);

					if (ITEM_TYPE_SNMPv3 == item.type)
					{
						item.snmpv3_securityname = strdup(dcheck->snmpv3_securityname);
						item.snmpv3_securitylevel = dcheck->snmpv3_securitylevel;
						item.snmpv3_authpassphrase = strdup(dcheck->snmpv3_authpassphrase);
						item.snmpv3_privpassphrase = strdup(dcheck->snmpv3_privpassphrase);

						substitute_simple_macros(NULL, NULL, NULL, NULL, NULL,
								&item.snmpv3_securityname, MACRO_TYPE_ITEM_FIELD, NULL, 0);
						substitute_simple_macros(NULL, NULL, NULL, NULL, NULL,
								&item.snmpv3_authpassphrase, MACRO_TYPE_ITEM_FIELD, NULL, 0);
						substitute_simple_macros(NULL, NULL, NULL, NULL, NULL,
								&item.snmpv3_privpassphrase, MACRO_TYPE_ITEM_FIELD, NULL, 0);
					}

					if(SUCCEED == get_value_snmp(&item, &result))
					{
						if (GET_STR_RESULT(&result))
							zbx_strlcpy(value, result.str, DSERVICE_VALUE_LEN_MAX);
						else
							ret = FAIL;
					}
					else
						ret = FAIL;

					zbx_free(item.snmp_community);
					zbx_free(item.snmp_oid);

					if (ITEM_TYPE_SNMPv3 == item.type)
					{
						zbx_free(item.snmpv3_securityname);
						zbx_free(item.snmpv3_authpassphrase);
						zbx_free(item.snmpv3_privpassphrase);
					}
				}
#else
					ret = FAIL;
#endif

				if (FAIL == ret && GET_MSG_RESULT(&result))
					zabbix_log(LOG_LEVEL_DEBUG, "Discovery: Item [%s] error: %s",
							item.key, result.msg);
				break;
			case SVC_ICMPPING:
				memset(&host, 0, sizeof(host));
				host.addr = strdup(ip);

				if (SUCCEED != do_ping(&host, 1, 3, 0, 0, 0, error, sizeof(error)) || 0 == host.rcv)
					ret = FAIL;

				zbx_free(host.addr);
				break;
			default:
				break;
		}
		alarm(0);
	}
	free_result(&result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_check                                                    *
 *                                                                            *
 * Purpose: check if service is available and update database                 *
 *                                                                            *
 * Parameters: service - service info                                         *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static void process_check(DB_DRULE *drule, DB_DCHECK *dcheck, DB_DHOST *dhost, int *host_status, char *ip)
{
	const char	*__function_name = "process_check";
	int		port, first, last, now;
	char		*curr_range, *next_range, *last_port;
	int		status;
	char		value[DSERVICE_VALUE_LEN_MAX];

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	for (curr_range = dcheck->ports; curr_range; curr_range = next_range)
	{	/* split by ',' */
		if (NULL != (next_range = strchr(curr_range, ',')))
			*next_range = '\0';

		if (NULL != (last_port = strchr(curr_range, '-')))
		{	/* split by '-' */
			*last_port	= '\0';
			first		= atoi(curr_range);
			last		= atoi(last_port + 1);
			*last_port	= '-';
		}
		else
			first = last	= atoi(curr_range);

		if (NULL != next_range)
		{
			*next_range = ',';
			next_range++;
		}

		for (port = first; port <= last; port++)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "%s() port:%d", __function_name, port);

			status = (SUCCEED == discover_service(dcheck, ip, port, value)) ? DOBJECT_STATUS_UP : DOBJECT_STATUS_DOWN;

			/* Update host status */
			if (*host_status == -1 || status == DOBJECT_STATUS_UP)
				*host_status = status;

			now = time(NULL);

			DBbegin();

			if (0 != (zbx_process & ZBX_PROCESS_SERVER))
			{
				discovery_update_service(drule, dcheck, dhost, ip, port, status, value, now);
			}
			else if (0 != (zbx_process & ZBX_PROCESS_PROXY))
			{
				proxy_update_service(drule, dcheck, ip, port, status, value, now);
			}

			DBcommit();
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: process_checks                                                   *
 *                                                                            *
 * Purpose:                                                                   *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Aleksander Vladishev                                               *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static void	process_checks(DB_DRULE *drule, DB_DHOST *dhost, int *host_status, char *ip, int unique)
{
	DB_RESULT	result;
	DB_ROW		row;
	DB_DCHECK	dcheck;
	char		sql[MAX_STRING_LEN];
	int		offset = 0;

	offset += zbx_snprintf(sql + offset, sizeof(sql) - offset,
			"select dcheckid,type,key_,snmp_community,snmpv3_securityname,snmpv3_securitylevel,"
				"snmpv3_authpassphrase,snmpv3_privpassphrase,ports"
			" from dchecks"
			" where druleid=" ZBX_FS_UI64,
			drule->druleid);

	if (drule->unique_dcheckid)
	{
		offset += zbx_snprintf(sql + offset, sizeof(sql) - offset,
				" and dcheckid%s" ZBX_FS_UI64,
				unique ? "=" : "<>",
				drule->unique_dcheckid);
	}

	offset += zbx_snprintf(sql + offset, sizeof(sql) - offset,
			" order by dcheckid");

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result))) {
		memset(&dcheck, 0, sizeof(dcheck));

		ZBX_STR2UINT64(dcheck.dcheckid, row[0]);
		dcheck.type			= atoi(row[1]);
		dcheck.key_			= row[2];
		dcheck.snmp_community		= row[3];
		dcheck.snmpv3_securityname	= row[4];
		dcheck.snmpv3_securitylevel	= atoi(row[5]);
		dcheck.snmpv3_authpassphrase	= row[6];
		dcheck.snmpv3_privpassphrase	= row[7];
		dcheck.ports			= row[8];

		process_check(drule, &dcheck, dhost, host_status, ip);
	}
	DBfree_result(result);
}

/******************************************************************************
 *                                                                            *
 * Function: process_rule                                                     *
 *                                                                            *
 * Purpose: process single discovery rule                                     *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static void process_rule(DB_DRULE *drule)
{
	const char	*__function_name = "process_rule";
	DB_DHOST	dhost;
	int		host_status, now;
	unsigned int	j[9], i, first, last, mask, network, broadcast;
	char		ip[HOST_IP_LEN_MAX], *curr_range, *next_range, *dash, *slash;
#if defined(HAVE_IPV6)
	int		ipv6;
#endif

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() rule:'%s' range:'%s'", __function_name,
			drule->name,
			drule->iprange);

	for (curr_range = drule->iprange; curr_range; curr_range = next_range)
	{ /* split by ',' */
		if (NULL != (next_range = strchr(curr_range, ',')))
			*next_range = '\0';

		zabbix_log(LOG_LEVEL_DEBUG, "%s() '%s'", __function_name,
				curr_range);

		if (NULL != (dash = strchr(curr_range, '-')))
			*dash = '\0';

		if (NULL != (slash = strchr(curr_range, '/')))
			*slash = '\0';

		first = last = 0;
#if defined(HAVE_IPV6)
		if (SUCCEED == expand_ipv6(curr_range, ip, sizeof(ip)))
		{
			ipv6 = 1;
			if (8 == sscanf(ip, "%x:%x:%x:%x:%x:%x:%x:%x", &j[0], &j[1], &j[2], &j[3], &j[4], &j[5], &j[6], &j[7]))
			{
				first = (j[6] << 16) + j[7];

				if (NULL != dash)
				{
					if (1 == sscanf(dash + 1, "%x", &j[8]))
						last = (j[6] << 16) + j[8];
				}
				else if (NULL != slash)
				{
					if (1 == sscanf(slash + 1, "%d", &j[8]) && j[8] >= 112 && j[8] <= 128)
					{
						j[8] -= 96;

						mask = (32 == j[8]) ? 0xffffffff : ~(0xffffffff >> j[8]);
						network = first & mask;
						broadcast = network + ~mask;
						first = network + 1;

						zabbix_log(LOG_LEVEL_DEBUG, "%s() IPv6 CIDR:%u", __function_name, j[8] + 96);
						zbx_snprintf(ip, sizeof(ip), "%x:%x:%x:%x:%x:%x:%x:%x",
								0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
								(mask & 0xffff0000) >> 16, (mask & 0x0000ffff));
						zabbix_log(LOG_LEVEL_DEBUG, "%s() IPv6 Netmask:'%s'",
								__function_name, collapse_ipv6(ip, sizeof(ip)));
						zbx_snprintf(ip, sizeof(ip), "%x:%x:%x:%x:%x:%x:%x:%x",
								j[0], j[1], j[2], j[3], j[4], j[5],
								(network & 0xffff0000) >> 16, (network & 0x0000ffff));
						zabbix_log(LOG_LEVEL_DEBUG, "%s() IPv6 Network:'%s'",
								__function_name, collapse_ipv6(ip, sizeof(ip)));
						zbx_snprintf(ip, sizeof(ip), "%x:%x:%x:%x:%x:%x:%x:%x",
								j[0], j[1], j[2], j[3], j[4], j[5],
								(broadcast & 0xffff0000) >> 16, (broadcast & 0x0000ffff));
						zabbix_log(LOG_LEVEL_DEBUG, "%s() IPv6 Broadcast:'%s'",
								__function_name, collapse_ipv6(ip, sizeof(ip)));

						if (j[8] <= 30)
							last = broadcast - 1;
					}
				}
				else
					last = first;

				zbx_snprintf(ip, sizeof(ip), "%x:%x:%x:%x:%x:%x:%x:%x",
						j[0], j[1], j[2], j[3], j[4], j[5],
						(first & 0xffff0000) >> 16, (first & 0x0000ffff));
				zabbix_log(LOG_LEVEL_DEBUG, "%s() IPv6 From:'%s'",
						__function_name, collapse_ipv6(ip, sizeof(ip)));
				zbx_snprintf(ip, sizeof(ip), "%x:%x:%x:%x:%x:%x:%x:%x",
						j[0], j[1], j[2], j[3], j[4], j[5],
						(last & 0xffff0000) >> 16, (last & 0x0000ffff));
				zabbix_log(LOG_LEVEL_DEBUG, "%s() IPv6 To:'%s'",
						__function_name, collapse_ipv6(ip, sizeof(ip)));
			}
		}
		else
		{
			ipv6 = 0;
#endif /* HAVE_IPV6 */
			if (4 == sscanf(curr_range, "%u.%u.%u.%u", &j[0], &j[1], &j[2], &j[3]) &&
					j[0] <= 255 && j[1] <= 255 && j[2] <= 255 && j[3] <= 255)
			{
				first = (j[0] << 24) + (j[1] << 16) + (j[2] << 8) + j[3];

				if (NULL != dash)
				{
					if (1 == sscanf(dash + 1, "%u", &j[4]) && j[4] <= 255)
						last = (j[0] << 24) + (j[1] << 16) + (j[2] << 8) + j[4];
				}
				else if (NULL != slash)
				{
					if (1 == sscanf(slash + 1, "%u", &j[4]) && j[4] >= 16 && j[4] <= 32)
					{
						mask = (32 == j[4]) ? 0xffffffff : ~(0xffffffff >> j[4]);
						network = first & mask;
						broadcast = network + ~mask;
						first = network + 1;

						zabbix_log(LOG_LEVEL_DEBUG, "%s() IPv4 CIDR:%u", __function_name, j[4]);
						zabbix_log(LOG_LEVEL_DEBUG, "%s() IPv4 Netmask:'%u.%u.%u.%u'", __function_name,
								(mask & 0xff000000) >> 24, (mask & 0x00ff0000) >> 16,
								(mask & 0x0000ff00) >> 8, (mask & 0x000000ff));
						zabbix_log(LOG_LEVEL_DEBUG, "%s() IPv4 Network:'%u.%u.%u.%u'", __function_name,
								(network & 0xff000000) >> 24, (network & 0x00ff0000) >> 16,
								(network & 0x0000ff00) >> 8, (network & 0x000000ff));
						zabbix_log(LOG_LEVEL_DEBUG, "%s() IPv4 Broadcast:'%u.%u.%u.%u'", __function_name,
								(broadcast & 0xff000000) >> 24, (broadcast & 0x00ff0000) >> 16,
								(broadcast & 0x0000ff00) >> 8, (broadcast & 0x000000ff));

						if (j[4] <= 30)
							last = broadcast - 1;
					}
				}
				else
					last = first;

				zabbix_log(LOG_LEVEL_DEBUG, "%s() IPv4 Range:'%u.%u.%u.%u' - '%u.%u.%u.%u'", __function_name,
						(first & 0xff000000) >> 24, (first & 0x00ff0000) >> 16,
						(first & 0x0000ff00) >> 8, (first & 0x000000ff),
						(last & 0xff000000) >> 24, (last & 0x00ff0000) >> 16,
						(last & 0x0000ff00) >> 8, (last & 0x000000ff));
			}
#if defined(HAVE_IPV6)
		}
#endif /* HAVE_IPV6 */

		if (NULL != dash)
		{
			*dash = '-';
			dash = NULL;
		}

		if (NULL != slash)
		{
			*slash = '/';
			slash = NULL;
		}

		if (NULL != next_range)
		{
			*next_range = ',';
			next_range ++;
		}

		if (first == 0 || last == 0 || first > last)
		{
			zabbix_log(LOG_LEVEL_WARNING, "Discovery: Wrong format of IP range '%s'",
					curr_range);
			continue;
		}

		for (i = first; i <= last; i++) {
			memset(&dhost, 0, sizeof(dhost));
			host_status	= -1;

			now = time(NULL);

#if defined(HAVE_IPV6)
			switch(ipv6) {
			case 0 :
#endif /* HAVE_IPV6 */
				zbx_snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
						(i & 0xff000000) >> 24,
						(i & 0x00ff0000) >> 16,
						(i & 0x0000ff00) >> 8,
						(i & 0x000000ff));
#if defined(HAVE_IPV6)
				break;
			case 1 :
				zbx_snprintf(ip, sizeof(ip), "%x:%x:%x:%x:%x:%x:%x:%x",
						j[0], j[1], j[2], j[3], j[4], j[5],
						(i & 0xffff0000) >> 16, (i & 0x0000ffff));
				collapse_ipv6(ip, sizeof(ip));
				break;
			}
#endif /* HAVE_IPV6 */

			zabbix_log(LOG_LEVEL_DEBUG, "%s() IP:'%s'", __function_name, ip);

			if (drule->unique_dcheckid)
				process_checks(drule, &dhost, &host_status, ip, 1);
			process_checks(drule, &dhost, &host_status, ip, 0);

			DBbegin();

			if (0 != (zbx_process & ZBX_PROCESS_SERVER))
			{
				discovery_update_host(&dhost, ip, host_status, now);
			}
			else if (0 != (zbx_process & ZBX_PROCESS_PROXY))
			{
				proxy_update_host(drule, ip, host_status, now);
			}

			DBcommit();
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

static void	process_discovery(int now)
{
	DB_RESULT	result;
	DB_ROW		row;
	DB_DRULE	drule;

	result = DBselect("select druleid,iprange,name,unique_dcheckid from drules"
			" where proxy_hostid=0 and status=%d and (nextcheck<=%d or nextcheck>%d+delay)"
			" and " ZBX_SQL_MOD(druleid,%d) "=%d" DB_NODE,
			DRULE_STATUS_MONITORED,
			now,
			now,
			CONFIG_DISCOVERER_FORKS,
			process_num - 1,
			DBnode_local("druleid"));

	while (NULL != (row = DBfetch(result))) {
		memset(&drule, 0, sizeof(drule));

		ZBX_STR2UINT64(drule.druleid, row[0]);
		drule.iprange 	= row[1];
		drule.name	= row[2];
		ZBX_STR2UINT64(drule.unique_dcheckid, row[3]);

		process_rule(&drule);

		DBexecute("update drules set nextcheck=%d+delay where druleid=" ZBX_FS_UI64,
				now,
				drule.druleid);
	}
	DBfree_result(result);
}

static int	get_minnextcheck(int now)
{
	DB_RESULT	result;
	DB_ROW		row;
	int		res = FAIL;

	result = DBselect("select count(*),min(nextcheck) from drules where proxy_hostid=0 and status=%d"
			" and " ZBX_SQL_MOD(druleid,%d) "=%d" DB_NODE,
			DRULE_STATUS_MONITORED,
			CONFIG_DISCOVERER_FORKS,
			process_num - 1,
			DBnode_local("druleid"));

	row = DBfetch(result);

	if (NULL == row || DBis_null(row[0]) == SUCCEED || DBis_null(row[1]) == SUCCEED)
		zabbix_log(LOG_LEVEL_DEBUG, "No items to update for minnextcheck.");
	else if (0 != atoi(row[0]))
		res = atoi(row[1]);

	DBfree_result(result);

	return res;
}

/******************************************************************************
 *                                                                            *
 * Function: main_discoverer_loop                                             *
 *                                                                            *
 * Purpose: periodically try to find new hosts and services                   *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: executes once per 30 seconds (hardcoded)                         *
 *                                                                            *
 ******************************************************************************/
void	main_discoverer_loop(unsigned char p)
{
	int	now, nextcheck, sleeptime;
	double	sec;

	zabbix_log(LOG_LEVEL_DEBUG, "In main_discoverer_loop() process_num:%d", process_num);

	set_child_signal_handler();

	zbx_process = p;

	zbx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));

	DBconnect(ZBX_DB_CONNECT_NORMAL);

	for (;;)
	{
		zbx_setproctitle("%s [discovering network]", get_process_type_string(process_type));

		now = time(NULL);
		sec = zbx_time();
		process_discovery(now);
		sec = zbx_time() - sec;

		zabbix_log(LOG_LEVEL_DEBUG, "%s #%d spent " ZBX_FS_DBL " seconds while processing rules",
				get_process_type_string(process_type), process_num, sec);

		nextcheck = get_minnextcheck(now);
		sleeptime = calculate_sleeptime(nextcheck, DISCOVERER_DELAY);

		zbx_sleep_loop(sleeptime);
	}
}
