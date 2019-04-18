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

#include "common.h"
#include "log.h"
#include "zbxregexp.h"
#include "winmeta.h"
#include "eventlog.h"

#define	DEFAULT_EVENT_CONTENT_SIZE 256

static const wchar_t	*RENDER_ITEMS[] = {
	L"/Event/System/Provider/@Name",
	L"/Event/System/Provider/@EventSourceName",
	L"/Event/System/EventRecordID",
	L"/Event/System/EventID",
	L"/Event/System/Level",
	L"/Event/System/Keywords",
	L"/Event/System/TimeCreated/@SystemTime",
	L"/Event/EventData/Data"
};

#define	RENDER_ITEMS_COUNT (sizeof(RENDER_ITEMS) / sizeof(const wchar_t *))

#define	VAR_PROVIDER_NAME(p)			(p[0].StringVal)
#define	VAR_SOURCE_NAME(p)			(p[1].StringVal)
#define	VAR_RECORD_NUMBER(p)			(p[2].UInt64Val)
#define	VAR_EVENT_ID(p)				(p[3].UInt16Val)
#define	VAR_LEVEL(p)				(p[4].ByteVal)
#define	VAR_KEYWORDS(p)				(p[5].UInt64Val)
#define	VAR_TIME_CREATED(p)			(p[6].FileTimeVal)
#define	VAR_EVENT_DATA_STRING(p)		(p[7].StringVal)
#define	VAR_EVENT_DATA_STRING_ARRAY(p, i)	(p[7].StringArr[i])
#define	VAR_EVENT_DATA_TYPE(p)			(p[7].Type)
#define	VAR_EVENT_DATA_COUNT(p)			(p[7].Count)

#define	EVENTLOG_REG_PATH TEXT("SYSTEM\\CurrentControlSet\\Services\\EventLog\\")

/* open event logger and return number of records */
static int	zbx_open_eventlog(LPCTSTR wsource, HANDLE *eventlog_handle, zbx_uint64_t *FirstID,
		zbx_uint64_t *LastID, DWORD *dwErr)
{
	const char	*__function_name = "zbx_open_eventlog";
	wchar_t		reg_path[MAX_PATH];
	HKEY		hk = NULL;
	DWORD		dwNumRecords, dwOldestRecord;
	int		ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	*eventlog_handle = NULL;

	/* Get path to eventlog */
	StringCchPrintf(reg_path, MAX_PATH, EVENTLOG_REG_PATH TEXT("%s"), wsource);

	if (ERROR_SUCCESS != RegOpenKeyEx(HKEY_LOCAL_MACHINE, reg_path, 0, KEY_READ, &hk))
	{
		*dwErr = GetLastError();
		goto out;
	}

	RegCloseKey(hk);

	if (NULL == (*eventlog_handle = OpenEventLog(NULL, wsource)))	/* open log file */
	{
		*dwErr = GetLastError();
		goto out;
	}

	if (0 == GetNumberOfEventLogRecords(*eventlog_handle, &dwNumRecords) ||
			0 == GetOldestEventLogRecord(*eventlog_handle, &dwOldestRecord))
	{
		*dwErr = GetLastError();
		CloseEventLog(*eventlog_handle);
		*eventlog_handle = NULL;
		goto out;
	}

	*FirstID = dwOldestRecord;
	*LastID = dwOldestRecord + dwNumRecords - 1;

	zabbix_log(LOG_LEVEL_DEBUG, "FirstID:" ZBX_FS_UI64 " LastID:" ZBX_FS_UI64 " numIDs:%lu",
			*FirstID, *LastID, dwNumRecords);

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/* close event logger */
static void	zbx_close_eventlog(HANDLE eventlog_handle)
{
	if (NULL != eventlog_handle)
		CloseEventLog(eventlog_handle);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_get_message_files                                            *
 *                                                                            *
 * Purpose: gets event message and parameter translation files from registry  *
 *                                                                            *
 * Parameters: szLogName         - [IN] the log name                          *
 *             szSourceName      - [IN] the log source name                   *
 *             pEventMessageFile - [OUT] the event message file               *
 *             pParamMessageFile - [OUT] the parameter message file           *
 *                                                                            *
 ******************************************************************************/
static void	zbx_get_message_files(const wchar_t *szLogName, const wchar_t *szSourceName, wchar_t **pEventMessageFile,
		wchar_t **pParamMessageFile)
{
	wchar_t	buf[MAX_PATH];
	HKEY	hKey = NULL;
	DWORD	szData;

	/* Get path to message dll */
	StringCchPrintf(buf, MAX_PATH, EVENTLOG_REG_PATH TEXT("%s\\%s"), szLogName, szSourceName);

	if (ERROR_SUCCESS != RegOpenKeyEx(HKEY_LOCAL_MACHINE, buf, 0, KEY_READ, &hKey))
		return;

	if (ERROR_SUCCESS == RegQueryValueEx(hKey, TEXT("EventMessageFile"), NULL, NULL, NULL, &szData))
	{
		*pEventMessageFile = zbx_malloc(*pEventMessageFile, szData);
		if (ERROR_SUCCESS != RegQueryValueEx(hKey, TEXT("EventMessageFile"), NULL, NULL,
				(unsigned char *)*pEventMessageFile, &szData))
		{
			zbx_free(*pEventMessageFile);
		}
	}

	if (ERROR_SUCCESS == RegQueryValueEx(hKey, TEXT("ParameterMessageFile"), NULL, NULL, NULL, &szData))
	{
		*pParamMessageFile = zbx_malloc(*pParamMessageFile, szData);
		if (ERROR_SUCCESS != RegQueryValueEx(hKey, TEXT("ParameterMessageFile"), NULL, NULL,
				(unsigned char *)*pParamMessageFile, &szData))
		{
			zbx_free(*pParamMessageFile);
		}
	}

	RegCloseKey(hKey);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_load_message_file                                            *
 *                                                                            *
 * Purpose: load the specified message file, expanding environment variables  *
 *          in the file name if necessary                                     *
 *                                                                            *
 * Parameters: szFileName - [IN] the message file name                        *
 *                                                                            *
 * Return value: Handle to the loaded library or NULL otherwise               *
 *                                                                            *
 ******************************************************************************/
static HINSTANCE	zbx_load_message_file(const wchar_t *szFileName)
{
	wchar_t		*dll_name = NULL;
	long int	sz, len = 0;
	HINSTANCE	res = NULL;

	if (NULL == szFileName)
		return NULL;

	do
	{
		if (0 != (sz = len))
			dll_name = zbx_realloc(dll_name, sz * sizeof(wchar_t));

		len = ExpandEnvironmentStrings(szFileName, dll_name, sz);
	}
	while (0 != len && sz < len);

	if (0 != len)
		res = LoadLibraryEx(dll_name, NULL, LOAD_LIBRARY_AS_DATAFILE);

	zbx_free(dll_name);

	return res;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_format_message                                               *
 *                                                                            *
 * Purpose: extracts the specified message from a message file                *
 *                                                                            *
 * Parameters: hLib           - [IN] the message file handle                  *
 *             dwMessageId    - [IN] the message identifier                   *
 *             pInsertStrings - [IN] a list of insert strings, optional       *
 *                                                                            *
 * Return value: The formatted message converted to utf8 or NULL              *
 *                                                                            *
 * Comments: This function allocates memory for the returned message, which   *
 *           must be freed by the caller later.                               *
 *                                                                            *
 ******************************************************************************/
static char	*zbx_format_message(HINSTANCE hLib, DWORD dwMessageId, wchar_t **pInsertStrings)
{
	wchar_t *pMsgBuf = NULL;
	char	*message;

	if (0 == FormatMessage(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_MAX_WIDTH_MASK,
			hLib, dwMessageId, MAKELANGID(LANG_NEUTRAL, SUBLANG_ENGLISH_US), (wchar_t *)&pMsgBuf, 0,
			(va_list *)pInsertStrings))
	{
		return NULL;
	}

	message = zbx_unicode_to_utf8(pMsgBuf);
	zbx_rtrim(message, "\r\n ");

	LocalFree((HLOCAL)pMsgBuf);

	return message;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_translate_message_params                                     *
 *                                                                            *
 * Purpose: translates message by replacing parameters %%<id> with translated *
 *          values                                                            *
 *                                                                            *
 * Parameters: message - [IN/OUT] the message to translate                    *
 *             hLib    - [IN] the parameter message file handle               *
 *                                                                            *
 ******************************************************************************/
static void	zbx_translate_message_params(char **message, HINSTANCE hLib)
{
	char	*param, *pstart, *pend;
	int	dwMessageId;
	size_t	offset = 0;

	while (1)
	{
		if (NULL == (pstart = strstr(*message + offset, "%%")))
			break;

		pend = pstart + 2;

		dwMessageId = atoi(pend);

		while ('\0' != *pend && 0 != isdigit(*pend))
			pend++;

		offset = pend - *message - 1;

		if (NULL != (param = zbx_format_message(hLib, dwMessageId, NULL)))
		{
			zbx_replace_string(message, pstart - *message, &offset, param);

			zbx_free(param);
		}
	}
}

/* open eventlog using API 6 and return the number of records */
static int	zbx_open_eventlog6(const wchar_t *wsource, zbx_uint64_t *lastlogsize, EVT_HANDLE *render_context,
		zbx_uint64_t *FirstID, zbx_uint64_t *LastID, char **error)
{
	const char	*__function_name = "zbx_open_eventlog6";
	EVT_HANDLE	log = NULL;
	EVT_VARIANT	var;
	EVT_HANDLE	tmp_all_event_query = NULL;
	EVT_HANDLE	event_bookmark = NULL;
	EVT_VARIANT*	renderedContent = NULL;
	DWORD		status = 0;
	DWORD		size_required = 0;
	DWORD		size = DEFAULT_EVENT_CONTENT_SIZE;
	DWORD		bookmarkedCount = 0;
	zbx_uint64_t	numIDs = 0;
	char		*tmp_str = NULL;
	int		ret = FAIL;

	*FirstID = 0;
	*LastID = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* try to open the desired log */
	if (NULL == (log = EvtOpenLog(NULL, wsource, EvtOpenChannelPath)))
	{
		status = GetLastError();
		tmp_str = zbx_unicode_to_utf8(wsource);
		*error = zbx_dsprintf(*error, "cannot open eventlog '%s':%s", tmp_str, strerror_from_system(status));
		goto out;
	}

	/* obtain the number of records in the log */
	if (TRUE != EvtGetLogInfo(log, EvtLogNumberOfLogRecords, sizeof(var), &var, &size_required))
	{
		*error = zbx_dsprintf(*error, "EvtGetLogInfo failed:%s", strerror_from_system(GetLastError()));
		goto out;
	}

	numIDs = var.UInt64Val;

	/* get the number of the oldest record in the log				*/
	/* "EvtGetLogInfo()" does not work properly with "EvtLogOldestRecordNumber"	*/
	/* we have to get it from the first EventRecordID				*/

	/* create the system render */
	if (NULL == (*render_context = EvtCreateRenderContext(RENDER_ITEMS_COUNT, RENDER_ITEMS, EvtRenderContextValues)))
	{
		*error = zbx_dsprintf(*error, "EvtCreateRenderContext failed:%s", strerror_from_system(GetLastError()));
		goto out;
	}

	/* get all eventlog */
	tmp_all_event_query = EvtQuery(NULL, wsource, NULL, EvtQueryChannelPath);
	if (NULL == tmp_all_event_query)
	{
		if (ERROR_EVT_CHANNEL_NOT_FOUND == (status = GetLastError()))
			*error = zbx_dsprintf(*error, "EvtQuery channel missed:%s", strerror_from_system(status));
		else
			*error = zbx_dsprintf(*error, "EvtQuery failed:%s", strerror_from_system(status));

		goto out;
	}

	/* get the entries and allocate the required space */
	renderedContent = zbx_malloc(renderedContent, size);
	if (TRUE != EvtNext(tmp_all_event_query, 1, &event_bookmark, INFINITE, 0, &size_required))
	{
		/* no data in eventlog */
		zabbix_log(LOG_LEVEL_DEBUG, "first EvtNext failed:%s", strerror_from_system(GetLastError()));
		*FirstID = 1;
		*LastID = 1;
		numIDs = 0;
		*lastlogsize = 0;
		ret = SUCCEED;
		goto out;
	}

	/* obtain the information from selected events */
	if (TRUE != EvtRender(*render_context, event_bookmark, EvtRenderEventValues, size, renderedContent,
			&size_required, &bookmarkedCount))
	{
		/* information exceeds the allocated space */
		if (ERROR_INSUFFICIENT_BUFFER != (status = GetLastError()))
		{
			*error = zbx_dsprintf(*error, "EvtRender failed:%s", strerror_from_system(status));
			goto out;
		}

		renderedContent = (EVT_VARIANT*)zbx_realloc((void *)renderedContent, size_required);
		size = size_required;

		if (TRUE != EvtRender(*render_context, event_bookmark, EvtRenderEventValues, size, renderedContent,
				&size_required, &bookmarkedCount))
		{
			*error = zbx_dsprintf(*error, "EvtRender failed:%s", strerror_from_system(GetLastError()));
			goto out;
		}
	}

	*FirstID = VAR_RECORD_NUMBER(renderedContent);
	*LastID = *FirstID + numIDs;

	if (*lastlogsize >= *LastID)
	{
		*lastlogsize = *FirstID - 1;
		zabbix_log(LOG_LEVEL_DEBUG, "lastlogsize is too big. It is set to:" ZBX_FS_UI64, *lastlogsize);
	}

	ret = SUCCEED;
out:
	if (NULL != log)
		EvtClose(log);
	if (NULL != tmp_all_event_query)
		EvtClose(tmp_all_event_query);
	if (NULL != event_bookmark)
		EvtClose(event_bookmark);
	zbx_free(tmp_str);
	zbx_free(renderedContent);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s FirstID:" ZBX_FS_UI64 " LastID:" ZBX_FS_UI64 " numIDs:" ZBX_FS_UI64,
			__function_name, zbx_result_string(ret), *FirstID, *LastID, numIDs);

	return ret;
}

/* get handles of eventlog */
static int	zbx_get_handle_eventlog6(const wchar_t *wsource, zbx_uint64_t *lastlogsize, EVT_HANDLE *query,
		char **error)
{
	const char	*__function_name = "zbx_get_handle_eventlog6";
	wchar_t		*event_query = NULL;
	DWORD		status = 0;
	char		*tmp_str = NULL;
	int		ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s(), previous lastlogsize:" ZBX_FS_UI64, __function_name, *lastlogsize);

	/* start building the query */
	tmp_str = zbx_dsprintf(NULL, "Event/System[EventRecordID>" ZBX_FS_UI64 "]", *lastlogsize);
	event_query = zbx_utf8_to_unicode(tmp_str);

	/* create massive query for an event on a local computer*/
	*query = EvtQuery(NULL, wsource, event_query, EvtQueryChannelPath);
	if (NULL == *query)
	{
		if (ERROR_EVT_CHANNEL_NOT_FOUND == (status = GetLastError()))
			*error = zbx_dsprintf(*error, "EvtQuery channel missed:%s", strerror_from_system(status));
		else
			*error = zbx_dsprintf(*error, "EvtQuery failed:%s", strerror_from_system(status));

		goto out;
	}

	ret = SUCCEED;
out:
	zbx_free(tmp_str);
	zbx_free(event_query);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/* initialize event logs with Windows API version 6 */
int	initialize_eventlog6(const char *source, zbx_uint64_t *lastlogsize, zbx_uint64_t *FirstID,
		zbx_uint64_t *LastID, EVT_HANDLE *render_context, EVT_HANDLE *query, char **error)
{
	const char	*__function_name = "initialize_eventlog6";
	wchar_t		*wsource = NULL;
	int		ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() source:'%s' previous lastlogsize:" ZBX_FS_UI64,
			__function_name, source, *lastlogsize);

	if (NULL == source || '\0' == *source)
	{
		*error = zbx_dsprintf(*error, "cannot open eventlog with empty name.");
		goto out;
	}

	wsource = zbx_utf8_to_unicode(source);

	if (SUCCEED != zbx_open_eventlog6(wsource, lastlogsize, render_context, FirstID, LastID, error))
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot open eventlog '%s'", source);
		goto out;
	}

	if (SUCCEED != zbx_get_handle_eventlog6(wsource, lastlogsize, query, error))
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot get eventlog handle '%s'", source);
		goto out;
	}

	ret = SUCCEED;
out:
	zbx_free(wsource);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/* expand the string message from a specific event handler */
static char	*expand_message6(const wchar_t *pname, EVT_HANDLE event)
{
	const char	*__function_name = "expand_message6";
	wchar_t		*pmessage = NULL;
	EVT_HANDLE	provider = NULL;
	DWORD		require = 0;
	char		*out_message = NULL;
	char		*tmp_pname = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (NULL == (provider = EvtOpenPublisherMetadata(NULL, pname, NULL, 0, 0)))
	{
		tmp_pname = zbx_unicode_to_utf8(pname);
		zabbix_log(LOG_LEVEL_DEBUG, "provider '%s' could not be opened: %s",
				strerror_from_system(GetLastError()), tmp_pname);
		zbx_free(tmp_pname);
		goto out;
	}

	if (TRUE != EvtFormatMessage(provider, event, 0, 0, NULL, EvtFormatMessageEvent, 0, NULL, &require))
	{
		if (ERROR_INSUFFICIENT_BUFFER == GetLastError())
		{
			DWORD	error = ERROR_SUCCESS;

			pmessage = zbx_malloc(pmessage, sizeof(WCHAR) * require);

			if (TRUE != EvtFormatMessage(provider, event, 0, 0, NULL, EvtFormatMessageEvent, require,
					pmessage, &require))
			{
				error = GetLastError();
			}

			if (ERROR_SUCCESS == error || ERROR_EVT_UNRESOLVED_VALUE_INSERT == error ||
					ERROR_EVT_UNRESOLVED_PARAMETER_INSERT == error ||
					ERROR_EVT_MAX_INSERTS_REACHED == error)
			{
				out_message = zbx_unicode_to_utf8(pmessage);
			}
			else
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot format message: %s", __function_name,
						strerror_from_system(error));
				goto out;
			}
		}
	}
out:
	if (NULL != provider)
		EvtClose(provider);
	zbx_free(pmessage);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, out_message);

	/* should be freed */
	return out_message;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_parse_eventlog_message6                                      *
 *                                                                            *
 * Purpose: details parse of a single EventLog record                         *
 *                                                                            *
 * Parameters: wsource        - [IN] EventLog file name                       *
 *             render_context - [IN] the handle to the rendering context      *
 *             event_bookmark - [IN/OUT] the handle of Event record for parse *
 *             which          - [IN/OUT] the position of the EventLog record  *
 *             out_severity   - [OUT] the ELR detail                          *
 *             out_timestamp  - [OUT] the ELR detail                          *
 *             out_provider   - [OUT] the ELR detail                          *
 *             out_source     - [OUT] the ELR detail                          *
 *             out_message    - [OUT] the ELR detail                          *
 *             out_eventid    - [OUT] the ELR detail                          *
 *             out_keywords   - [OUT] the ELR detail                          *
 *             error          - [OUT] the error message in the case of        *
 *                                    failure                                 *
 *                                                                            *
 * Return value: SUCCEED or FAIL                                              *
 *                                                                            *
 ******************************************************************************/
static int	zbx_parse_eventlog_message6(const wchar_t *wsource, EVT_HANDLE *render_context,
		EVT_HANDLE *event_bookmark, zbx_uint64_t *which, unsigned short *out_severity,
		unsigned long *out_timestamp, char **out_provider, char **out_source, char **out_message,
		unsigned long *out_eventid, zbx_uint64_t *out_keywords, char **error)
{
	const char		*__function_name = "zbx_parse_eventlog_message6";
	EVT_VARIANT*		renderedContent = NULL;
	const wchar_t		*pprovider = NULL;
	char			*tmp_str = NULL;
	DWORD			size = DEFAULT_EVENT_CONTENT_SIZE, bookmarkedCount = 0, require = 0, dwErr;
	const zbx_uint64_t	sec_1970 = 116444736000000000;
	const zbx_uint64_t	success_audit = 0x20000000000000;
	const zbx_uint64_t	failure_audit = 0x10000000000000;
	int			ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() EventRecordID:" ZBX_FS_UI64, __function_name, *which);

	/* obtain the information from the selected events */

	renderedContent = (EVT_VARIANT *)zbx_malloc((void *)renderedContent, size);

	if (TRUE != EvtRender(*render_context, *event_bookmark, EvtRenderEventValues, size, renderedContent,
			&require, &bookmarkedCount))
	{
		/* information exceeds the space allocated */
		if (ERROR_INSUFFICIENT_BUFFER != (dwErr = GetLastError()))
		{
			*error = zbx_dsprintf(*error, "EvtRender failed: %s", strerror_from_system(dwErr));
			goto out;
		}

		renderedContent = (EVT_VARIANT *)zbx_realloc((void *)renderedContent, require);
		size = require;

		if (TRUE != EvtRender(*render_context, *event_bookmark, EvtRenderEventValues, size, renderedContent,
				&require, &bookmarkedCount))
		{
			*error = zbx_dsprintf(*error, "EvtRender failed: %s", strerror_from_system(GetLastError()));
			goto out;
		}
	}

	pprovider = VAR_PROVIDER_NAME(renderedContent);
	*out_provider = zbx_unicode_to_utf8(pprovider);
	*out_source = NULL;

	if (NULL != VAR_SOURCE_NAME(renderedContent))
	{
		*out_source = zbx_unicode_to_utf8(VAR_SOURCE_NAME(renderedContent));
	}

	*out_keywords = VAR_KEYWORDS(renderedContent) & (success_audit | failure_audit);
	*out_severity = VAR_LEVEL(renderedContent);
	*out_timestamp = (unsigned long)((VAR_TIME_CREATED(renderedContent) - sec_1970) / 10000000);
	*out_eventid = VAR_EVENT_ID(renderedContent);
	*out_message = expand_message6(pprovider, *event_bookmark);

	tmp_str = zbx_unicode_to_utf8(wsource);

	if (VAR_RECORD_NUMBER(renderedContent) != *which)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() Overwriting expected EventRecordID:" ZBX_FS_UI64 " with the real"
				" EventRecordID:" ZBX_FS_UI64 " in eventlog '%s'", __function_name, *which,
				VAR_RECORD_NUMBER(renderedContent), tmp_str);
		*which = VAR_RECORD_NUMBER(renderedContent);
	}

	/* some events don't have enough information for making event message */
	if (NULL == *out_message)
	{
		*out_message = zbx_strdcatf(*out_message, "The description for Event ID:%lu in Source:'%s'"
				" cannot be found. Either the component that raises this event is not installed"
				" on your local computer or the installation is corrupted. You can install or repair"
				" the component on the local computer. If the event originated on another computer,"
				" the display information had to be saved with the event.", *out_eventid,
				NULL == *out_provider ? "" : *out_provider);

		if (EvtVarTypeString == (VAR_EVENT_DATA_TYPE(renderedContent) & EVT_VARIANT_TYPE_MASK))
		{
			unsigned int	i;
			char		*data = NULL;

			if (0 != (VAR_EVENT_DATA_TYPE(renderedContent) & EVT_VARIANT_TYPE_ARRAY) &&
				0 < VAR_EVENT_DATA_COUNT(renderedContent))
			{
				*out_message = zbx_strdcatf(*out_message, " The following information was included"
						" with the event: ");

				for (i = 0; i < VAR_EVENT_DATA_COUNT(renderedContent); i++)
				{
					if (NULL != VAR_EVENT_DATA_STRING_ARRAY(renderedContent, i))
					{
						if (0 < i)
							*out_message = zbx_strdcat(*out_message, "; ");

						data = zbx_unicode_to_utf8(VAR_EVENT_DATA_STRING_ARRAY(renderedContent,
								i));
						*out_message = zbx_strdcatf(*out_message, "%s", data);
						zbx_free(data);
					}
				}
			}
			else if (NULL != VAR_EVENT_DATA_STRING(renderedContent))
			{
				data = zbx_unicode_to_utf8(VAR_EVENT_DATA_STRING(renderedContent));
				*out_message = zbx_strdcatf(*out_message, "The following information was included"
						" with the event: %s", data);
				zbx_free(data);
			}
		}
	}

	ret = SUCCEED;
out:
	EvtClose(*event_bookmark);
	*event_bookmark = NULL;

	zbx_free(tmp_str);
	zbx_free(renderedContent);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_eventslog6                                               *
 *                                                                            *
 * Purpose:  batch processing of Event Log file                               *
 *                                                                            *
 * Parameters: server           - [IN] IP or Hostname of Zabbix server        *
 *             port             - [IN] port of Zabbix server                  *
 *             fl_source        - [IN] the name of the Event Log file         *
 *             render_context   - [IN] the handle to the rendering context    *
 *             query            - [IN] the handle to the query results        *
 *             lastlogsize      - [IN] position of the last processed record  *
 *             FirstID          - [IN] first record in the EventLog file      *
 *             LastID           - [IN] last record in the EventLog file       *
 *             regexps          - [IN] set of regexp rules for Event Log test *
 *             pattern          - [IN] buffer for read of data of EventLog    *
 *             key_severity     - [IN] severity of logged data sources        *
 *             key_source       - [IN] name of logged data source             *
 *             key_logeventid   - [IN] the application-specific identifier    *
 *                                     for the event                          *
 *             rate             - [IN] threshold of records count at a time   *
 *             process_value_cb - [IN] callback function for sending data to  *
 *                                     the server                             *
 *             metric           - [IN/OUT] parameters for EventLog process    *
 *             lastlogsize_sent - [OUT] position of the last record sent to   *
 *                                      the server                            *
 *             error            - [OUT] the error message in the case of      *
 *                                      failure                               *
 *                                                                            *
 * Return value: SUCCEED or FAIL                                              *
 *                                                                            *
 ******************************************************************************/
int	process_eventslog6(const char *server, unsigned short port, const char *fl_source, EVT_HANDLE *render_context,
		EVT_HANDLE *query, zbx_uint64_t lastlogsize, zbx_uint64_t FirstID, zbx_uint64_t LastID,
		zbx_vector_ptr_t *regexps, const char *pattern, const char *key_severity, const char *key_source,
		const char *key_logeventid, int rate, zbx_process_value_t process_value_cb, ZBX_ACTIVE_METRIC *metric,
		zbx_uint64_t *lastlogsize_sent, char **error)
{
#	define EVT_ARRAY_SIZE	100

	const char	*str_severity, *__function_name = "process_eventslog6";
	zbx_uint64_t	keywords, i, reading_startpoint = 0;
	wchar_t		*wsource = NULL;
	int		s_count = 0, p_count = 0, send_err = SUCCEED, ret = FAIL;
	DWORD		require = 0, dwErr = ERROR_SUCCESS;

	unsigned long	evt_timestamp, evt_eventid;
	char		*evt_provider, *evt_source, *evt_message, str_logeventid[8];
	unsigned short	evt_severity;
	EVT_HANDLE	event_bookmarks[EVT_ARRAY_SIZE];

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() source: '%s' previous lastlogsize: " ZBX_FS_UI64 ", FirstID: "
			ZBX_FS_UI64 ", LastID: " ZBX_FS_UI64, __function_name, fl_source, lastlogsize, FirstID,
			LastID);

	/* update counters */
	if (1 == metric->skip_old_data)
	{
		metric->lastlogsize = LastID - 1;
		metric->skip_old_data = 0;
		zabbix_log(LOG_LEVEL_DEBUG, "skipping existing data: lastlogsize:" ZBX_FS_UI64, lastlogsize);
		goto finish;
	}

	if (NULL == *query)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() no EvtQuery handle", __function_name);
		goto out;
	}

	if (lastlogsize >= FirstID && lastlogsize < LastID)
		reading_startpoint = lastlogsize + 1;
	else
		reading_startpoint = FirstID;

	if (reading_startpoint == LastID)	/* LastID = FirstID + count */
		goto finish;

	wsource = zbx_utf8_to_unicode(fl_source);

	while (ERROR_SUCCESS == dwErr)
	{
		/* get the entries */
		if (TRUE != EvtNext(*query, EVT_ARRAY_SIZE, event_bookmarks, INFINITE, 0, &require))
		{
			/* The event reading query had less items than we calculated before. */
			/* Either the eventlog was cleaned or our calculations were wrong.   */
			/* Either way we can safely abort the query by setting NULL value    */
			/* and returning success, which is interpreted as empty eventlog.    */
			if (ERROR_NO_MORE_ITEMS == (dwErr = GetLastError()))
				continue;

			*error = zbx_dsprintf(*error, "EvtNext failed: %s, EventRecordID:" ZBX_FS_UI64,
					strerror_from_system(dwErr), lastlogsize + 1);
			goto out;
		}

		for (i = 0; i < require; i++)
		{
			lastlogsize += 1;

			if (SUCCEED != zbx_parse_eventlog_message6(wsource, render_context, &event_bookmarks[i],
					&lastlogsize, &evt_severity, &evt_timestamp, &evt_provider, &evt_source,
					&evt_message, &evt_eventid, &keywords, error))
			{
				goto out;
			}

			switch (evt_severity)
			{
				case WINEVENT_LEVEL_LOG_ALWAYS:
				case WINEVENT_LEVEL_INFO:
					if (0 != (keywords & WINEVENT_KEYWORD_AUDIT_FAILURE))
					{
						evt_severity = ITEM_LOGTYPE_FAILURE_AUDIT;
						str_severity = AUDIT_FAILURE;
						break;
					}
					else if (0 != (keywords & WINEVENT_KEYWORD_AUDIT_SUCCESS))
					{
						evt_severity = ITEM_LOGTYPE_SUCCESS_AUDIT;
						str_severity = AUDIT_SUCCESS;
						break;
					}
					else
						evt_severity = ITEM_LOGTYPE_INFORMATION;
						str_severity = INFORMATION_TYPE;
						break;
				case WINEVENT_LEVEL_WARNING:
					evt_severity = ITEM_LOGTYPE_WARNING;
					str_severity = WARNING_TYPE;
					break;
				case WINEVENT_LEVEL_ERROR:
					evt_severity = ITEM_LOGTYPE_ERROR;
					str_severity = ERROR_TYPE;
					break;
				case WINEVENT_LEVEL_CRITICAL:
					evt_severity = ITEM_LOGTYPE_CRITICAL;
					str_severity = CRITICAL_TYPE;
					break;
				case WINEVENT_LEVEL_VERBOSE:
					evt_severity = ITEM_LOGTYPE_VERBOSE;
					str_severity = VERBOSE_TYPE;
					break;
			}

			zbx_snprintf(str_logeventid, sizeof(str_logeventid), "%lu", evt_eventid);

			if (SUCCEED == regexp_match_ex(regexps, evt_message, pattern, ZBX_CASE_SENSITIVE) &&
					SUCCEED == regexp_match_ex(regexps, str_severity, key_severity,
							ZBX_IGNORE_CASE) &&
					SUCCEED == regexp_match_ex(regexps, evt_provider, key_source,
							ZBX_IGNORE_CASE) &&
					SUCCEED == regexp_match_ex(regexps, str_logeventid, key_logeventid,
							ZBX_CASE_SENSITIVE))
			{
				send_err = process_value_cb(server, port, CONFIG_HOSTNAME, metric->key_orig,
						evt_message, ITEM_STATE_NORMAL, &lastlogsize, NULL, &evt_timestamp,
						evt_provider, &evt_severity, &evt_eventid,
						metric->flags | ZBX_METRIC_FLAG_PERSISTENT);

				if (SUCCEED == send_err)
				{
					*lastlogsize_sent = lastlogsize;
					s_count++;
				}
			}
			p_count++;

			zbx_free(evt_source);
			zbx_free(evt_provider);
			zbx_free(evt_message);

			if (SUCCEED == send_err)
			{
				metric->lastlogsize = lastlogsize;
			}
			else
			{
				/* buffer is full, stop processing active checks */
				/* till the buffer is cleared */
				break;
			}

			/* do not flood Zabbix server if file grows too fast */
			if (s_count >= (rate * metric->refresh))
				break;

			/* do not flood local system if file grows too fast */
			if (p_count >= (4 * rate * metric->refresh))
				break;
		}

		if (i < require)
			dwErr = ERROR_NO_MORE_ITEMS;
	}
finish:
	ret = SUCCEED;
out:
	for (i = 0; i < require; i++)
	{
		if (NULL != event_bookmarks[i])
			EvtClose(event_bookmarks[i]);
	}

	zbx_free(wsource);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/* finalize eventlog6 and free the handles */
int	finalize_eventlog6(EVT_HANDLE *render_context, EVT_HANDLE *query)
{
	const char	*__function_name = "finalize_eventlog6";
	int		ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (NULL != *query)
	{
		EvtClose(*query);
		*query = NULL;
	}

	if (NULL != *render_context)
	{
		EvtClose(*render_context);
		*render_context = NULL;
	}

	ret = SUCCEED;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: seek_eventlog                                                    *
 *                                                                            *
 * Purpose: the first pointer offset for read Event Log file                  *
 *                                                                            *
 * Parameters: eventlog_handle - [IN] the handle to the event log to be read  *
 *             FirstID         - [IN] the first Event log record to be parse  *
 *             ReadDirection   - [IN] direction of reading:                   *
 *                                    EVENTLOG_FORWARDS_READ or               *
 *                                    EVENTLOG_BACKWARDS_READ                 *
 *             LastID          - [IN] position of last record in EventLog     *
 *             eventlog_name   - [IN] the name of the event log               *
 *             pELRs           - [IN/OUT] buffer for read of data of EventLog *
 *             buffer_size     - [IN/OUT] size of the pELRs                   *
 *             dwRead          - [OUT] the number of bytes read from EventLog *
 *             dwErr           - [OUT] result of the pointer shift attempt    *
 *                                     state                                  *
 *             error           - [OUT] the error message in the case of       *
 *                                     failure                                *
 *                                                                            *
 * Return value: SUCCEED or FAIL                                              *
 *                                                                            *
 ******************************************************************************/
static int	seek_eventlog(HANDLE *eventlog_handle, zbx_uint64_t FirstID, DWORD ReadDirection,
		zbx_uint64_t LastID, const char *eventlog_name, BYTE **pELRs, int *buffer_size, DWORD *dwRead,
		DWORD *dwErr, char **error)
{
	const char	*__function_name="seek_eventlog";
	BYTE		*pEndOfRecords, *pELR;
	DWORD		dwRecordNumber, dwNeeded;
	zbx_uint64_t	skip_count = 0;

	/* convert to DWORD to handle possible event record number wraparound */
	dwRecordNumber = (DWORD)FirstID;

	*dwErr = ERROR_SUCCESS;

	while (ERROR_SUCCESS == *dwErr)
	{
		if (ReadEventLog(eventlog_handle, EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, dwRecordNumber, *pELRs,
				*buffer_size, dwRead, &dwNeeded))
		{
			return SUCCEED;
		}

		if (ERROR_INVALID_PARAMETER == (*dwErr = GetLastError()))
		{
			/* see Microsoft Knowledge Base article, 177199 "BUG: ReadEventLog Fails with Error 87" */
			/* how ReadEventLog() can fail with all valid parameters */
			break;
		}
		else if (ERROR_HANDLE_EOF == *dwErr)
		{
			return SUCCEED;
		}
		else if (ERROR_INSUFFICIENT_BUFFER == *dwErr)
		{
			*buffer_size = dwNeeded;
			*pELRs = (BYTE *)zbx_realloc((void *)*pELRs, *buffer_size);
			*dwErr = ERROR_SUCCESS;
		}
		else
		{
			*error = zbx_dsprintf(*error, "Cannot read eventlog '%s': %s.", eventlog_name,
					strerror_from_system(*dwErr));
			return FAIL;
		}
	}

	/* Fallback implementation of the first seek for read pointer of EventLog. */
	if (ERROR_INVALID_PARAMETER == *dwErr && EVENTLOG_BACKWARDS_READ == ReadDirection)
	{
		if (LastID == FirstID)
			skip_count = 1;
		else
			skip_count = LastID - FirstID;

		zabbix_log(LOG_LEVEL_DEBUG, "In %s(): fallback dwErr=%d skip_count="ZBX_FS_UI64, __function_name,
				*dwErr, skip_count);
	}

	*dwErr = ERROR_SUCCESS;

	while (0 < skip_count && ERROR_SUCCESS == *dwErr && EVENTLOG_BACKWARDS_READ == ReadDirection)
	{
		if (!ReadEventLog(eventlog_handle, EVENTLOG_SEQUENTIAL_READ | ReadDirection, 0, *pELRs, *buffer_size,
				dwRead, &dwNeeded))
		{
			if (ERROR_INSUFFICIENT_BUFFER == (*dwErr = GetLastError()))
			{
				*dwErr = ERROR_SUCCESS;
				*buffer_size = dwNeeded;
				*pELRs = (BYTE *)zbx_realloc((void *)*pELRs, *buffer_size);
				continue;
			}
			else if (ERROR_HANDLE_EOF != *dwErr)
				break;

			*error = zbx_dsprintf(*error, "Cannot read eventlog '%s': %s.", eventlog_name,
					strerror_from_system(*dwErr));
			return FAIL;
		}

		pELR = *pELRs;
		pEndOfRecords = *pELRs + *dwRead;
		*dwRead = 0;	/* we can't reuse the buffer value because of the sort order */

		while (pELR < pEndOfRecords)
		{
			if (0 == --skip_count)
				break;

			pELR += ((PEVENTLOGRECORD)pELR)->Length;
		}
	}

	if (EVENTLOG_BACKWARDS_READ == ReadDirection && ERROR_HANDLE_EOF == *dwErr)
		*dwErr = ERROR_SUCCESS;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_parse_eventlog_message                                       *
 *                                                                            *
 * Purpose: details parse of a single Event Log record                        *
 *                                                                            *
 * Parameters: wsource       - [IN] EventLog file name                        *
 *             pELR          - [IN] buffer with single Event Log Record       *
 *             out_source    - [OUT] the ELR detail                           *
 *             out_message   - [OUT] the ELR detail                           *
 *             out_severity  - [OUT] the ELR detail                           *
 *             out_timestamp - [OUT] the ELR detail                           *
 *             out_eventid   - [OUT] the ELR detail                           *
 *                                                                            *
 ******************************************************************************/
#define MAX_INSERT_STRS 100
static void	zbx_parse_eventlog_message(const wchar_t *wsource, const EVENTLOGRECORD *pELR, char **out_source,
		char **out_message, unsigned short *out_severity, unsigned long *out_timestamp,
		unsigned long *out_eventid)
{
	const char	*__function_name = "zbx_parse_eventlog_message";
	wchar_t 	*pEventMessageFile = NULL, *pParamMessageFile = NULL, *pFile = NULL, *pNextFile = NULL, *pCh,
			*aInsertStrings[MAX_INSERT_STRS];
	HINSTANCE	hLib = NULL, hParamLib = NULL;
	long		i;
	int		err;

	memset(aInsertStrings, 0, sizeof(aInsertStrings));

	*out_message = NULL;
	*out_severity = pELR->EventType;				/* return event type */
	*out_timestamp = pELR->TimeGenerated;				/* return timestamp */
	*out_eventid = pELR->EventID & 0xffff;
	*out_source = zbx_unicode_to_utf8((wchar_t *)(pELR + 1));	/* copy source name */

	/* get message file names */
	zbx_get_message_files(wsource, (wchar_t *)(pELR + 1), &pEventMessageFile, &pParamMessageFile);

	/* prepare insert string array */
	if (0 < pELR->NumStrings)
	{
		pCh = (wchar_t *)((unsigned char *)pELR + pELR->StringOffset);

		for (i = 0; i < pELR->NumStrings && i < MAX_INSERT_STRS; i++)
		{
			aInsertStrings[i] = pCh;
			pCh += wcslen(pCh) + 1;
		}
	}

	err = FAIL;

	for (pFile = pEventMessageFile; NULL != pFile && err != SUCCEED; pFile = pNextFile)
	{
		if (NULL != (pNextFile = wcschr(pFile, TEXT(';'))))
		{
			*pNextFile = '\0';
			pNextFile++;
		}

		if (NULL != (hLib = zbx_load_message_file(pFile)))
		{
			if (NULL != (*out_message = zbx_format_message(hLib, pELR->EventID, aInsertStrings)))
			{
				err = SUCCEED;

				if (NULL != (hParamLib = zbx_load_message_file(pParamMessageFile)))
				{
					zbx_translate_message_params(out_message, hParamLib);
					FreeLibrary(hParamLib);
				}
			}

			FreeLibrary(hLib);
		}
	}

	zbx_free(pEventMessageFile);
	zbx_free(pParamMessageFile);

	if (SUCCEED != err)
	{
		*out_message = zbx_strdcatf(*out_message, "The description for Event ID:%lu in Source:'%s'"
				" cannot be found. The local computer may not have the necessary registry"
				" information or message DLL files to display messages from a remote computer.",
				*out_eventid, NULL == *out_source ? "" : *out_source);

		if (0 < pELR->NumStrings)
		{
			char	*buf;

			*out_message = zbx_strdcat(*out_message, " The following information is part of the event: ");

			for (i = 0, pCh = (wchar_t *)((unsigned char *)pELR + pELR->StringOffset);
					i < pELR->NumStrings;
					i++, pCh += wcslen(pCh) + 1)
			{
				if (0 < i)
					*out_message = zbx_strdcat(*out_message, "; ");

				buf = zbx_unicode_to_utf8(pCh);
				*out_message = zbx_strdcat(*out_message, buf);
				zbx_free(buf);
			}
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return;
}

/******************************************************************************
 *                                                                            *
 * Function: process_eventslog                                                *
 *                                                                            *
 * Purpose:  batch processing of Event Log file                               *
 *                                                                            *
 * Parameters: server           - [IN] IP or Hostname of Zabbix server        *
 *             port             - [IN] port of Zabbix server                  *
 *             eventlog_name    - [IN] the name of the event log              *
 *             regexps          - [IN] set of regexp rules for Event Log test *
 *             pattern          - [IN] buffer for read of data of EventLog    *
 *             key_severity     - [IN] severity of logged data sources        *
 *             key_source       - [IN] name of logged data source             *
 *             key_logeventid   - [IN] the application-specific identifier    *
 *                                     for the event                          *
 *             rate             - [IN] threshold of records count at a time   *
 *             process_value_cb - [IN] callback function for sending data to  *
 *                                     the server                             *
 *             metric           - [IN/OUT] parameters for EventLog process    *
 *             lastlogsize_sent - [OUT] position of the last record sent to   *
 *                                      the server                            *
 *             error            - [OUT] the error message in the case of      *
 *                                     failure                                *
 *                                                                            *
 * Return value: SUCCEED or FAIL                                              *
 *                                                                            *
 ******************************************************************************/
int	process_eventslog(const char *server, unsigned short port, const char *eventlog_name, zbx_vector_ptr_t *regexps,
		const char *pattern, const char *key_severity, const char *key_source, const char *key_logeventid,
		int rate, zbx_process_value_t process_value_cb, ZBX_ACTIVE_METRIC *metric,
		zbx_uint64_t *lastlogsize_sent, char **error)
{
	const char	*str_severity, *__function_name = "process_eventslog";
	int		ret = FAIL;
	HANDLE		eventlog_handle = NULL;
	wchar_t 	*eventlog_name_w;
	zbx_uint64_t	FirstID, LastID, lastlogsize;
	int		buffer_size = 64 * ZBX_KIBIBYTE;
	DWORD		dwRead = 0, dwNeeded, ReadDirection, dwErr;
	BYTE		*pELR, *pEndOfRecords, *pELRs = NULL;
	int		s_count, p_count, send_err = SUCCEED;
	unsigned long	logeventid, timestamp = 0;
	unsigned short	severity;
	char		*source, *value, str_logeventid[8];

	lastlogsize = metric->lastlogsize;
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() source:'%s' lastlogsize:" ZBX_FS_UI64, __function_name, eventlog_name,
			lastlogsize);

	/* From MSDN documentation:                                                                         */
	/* The RecordNumber member of EVENTLOGRECORD contains the record number for the event log record.   */
	/* The very first record written to an event log is record number 1, and other records are          */
	/* numbered sequentially. If the record number reaches ULONG_MAX, the next record number will be 0, */
	/* not 1; however, you use zero to seek to the record.                                              */
	/*                                                                                                  */
	/* This RecordNumber wraparound is handled simply by using 64bit integer to calculate record        */
	/* numbers and then converting to DWORD values.                                                     */

	if (NULL == eventlog_name || '\0' == *eventlog_name)
	{
		*error = zbx_strdup(*error, "Cannot open eventlog with empty name.");
		return ret;
	}

	eventlog_name_w = zbx_utf8_to_unicode(eventlog_name);

	if (SUCCEED != zbx_open_eventlog(eventlog_name_w, &eventlog_handle, &FirstID, &LastID, &dwErr))
	{
		*error = zbx_dsprintf(*error, "Cannot open eventlog '%s': %s.", eventlog_name,
				strerror_from_system(dwErr));
		goto out;
	}

	if (1 == metric->skip_old_data)
	{
		metric->lastlogsize = LastID;
		metric->skip_old_data = 0;
		zabbix_log(LOG_LEVEL_DEBUG, "skipping existing data: lastlogsize:" ZBX_FS_UI64, metric->lastlogsize);
		goto finish;
	}

	/* Having lastlogsize greater than LastID means that there was oldest event record */
	/* (FirstID) wraparound. In this case we must also wrap the lastlogsize value.     */
	if (lastlogsize > LastID)
		lastlogsize = (DWORD)lastlogsize;

	ReadDirection = ((LastID - FirstID) / 2) > lastlogsize ? EVENTLOG_FORWARDS_READ : EVENTLOG_BACKWARDS_READ;

	/* if the lastlogsize is still outside log record interval reset it to the oldest record number, */
	/* otherwise set FirstID to the next record after lastlogsize, which is the first event record   */
	/* to read                                                                                       */
	if (lastlogsize > LastID || lastlogsize < FirstID)
	{
		lastlogsize = FirstID;
		ReadDirection = 0;
	}
	else
		FirstID = lastlogsize + 1;

	pELRs = (BYTE*)zbx_malloc((void *)pELRs, buffer_size);

	if (0 == ReadDirection)		/* read eventlog from the first record */
	{
		dwErr = ERROR_SUCCESS;
	}
	else if (LastID < FirstID)	/* no new records */
	{
		dwErr = ERROR_HANDLE_EOF;
	}
	else if (SUCCEED != seek_eventlog(eventlog_handle, FirstID, ReadDirection, LastID, eventlog_name, &pELRs,
			&buffer_size, &dwRead, &dwErr, error))
	{
		goto out;
	}

	zabbix_log(LOG_LEVEL_TRACE, "%s(): state before EventLog reading: dwRead=%d dwErr=%s FirstID=" ZBX_FS_UI64
			" LastID=" ZBX_FS_UI64 " lastlogsize=" ZBX_FS_UI64, __function_name, dwRead,
			strerror_from_system(dwErr), FirstID, LastID, lastlogsize);

	if (ERROR_HANDLE_EOF == dwErr)
		goto finish;

	s_count = 0;
	p_count = 0;

	/* Read blocks of records until you reach the end of the log or an           */
	/* error occurs. The records are read from oldest to newest. If the buffer   */
	/* is not big enough to hold a complete event record, reallocate the buffer. */
	while (ERROR_SUCCESS == dwErr)
	{
		if (0 == dwRead && !ReadEventLog(eventlog_handle, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_FORWARDS_READ, 0,
				pELRs, buffer_size, &dwRead, &dwNeeded))
		{
			if (ERROR_INSUFFICIENT_BUFFER == (dwErr = GetLastError()))
			{
				dwErr = ERROR_SUCCESS;
				buffer_size = dwNeeded;
				pELRs = (BYTE *)zbx_realloc((void *)pELRs, buffer_size);
				continue;
			}
			else if (ERROR_HANDLE_EOF == dwErr)
				break;

			*error = zbx_dsprintf(*error, "Cannot read eventlog '%s': %s.", eventlog_name,
					strerror_from_system(dwErr));
			goto out;
		}

		pELR = pELRs;
		pEndOfRecords = pELR + dwRead;
		zabbix_log(LOG_LEVEL_TRACE, "%s(): state before buffer parsing: dwRead = %d RecordNumber = %d"
				"FirstID = "ZBX_FS_UI64" LastID = "ZBX_FS_UI64" lastlogsize="ZBX_FS_UI64,
				__function_name, dwRead, ((PEVENTLOGRECORD)pELR)->RecordNumber, FirstID, LastID,
				lastlogsize);
		dwRead = 0;

		while (pELR < pEndOfRecords)
		{
			/* to prevent mismatch in comparing with RecordNumber in case of wrap-around, */
			/* we look for using '=' */
			if (0 != timestamp || (DWORD)FirstID == ((PEVENTLOGRECORD)pELR)->RecordNumber)
			{
				/* increase counter only for records >= FirstID (start point for the search) */
				/* to avoid wrap-around of the 32b RecordNumber we increase the 64b lastlogsize */
				if (0 == timestamp)
					lastlogsize = FirstID;
				else
					lastlogsize += 1;

				zbx_parse_eventlog_message(eventlog_name_w, (EVENTLOGRECORD *)pELR, &source, &value,
						&severity, &timestamp, &logeventid);

				switch (severity)
				{
					case EVENTLOG_SUCCESS:
					case EVENTLOG_INFORMATION_TYPE:
						severity = ITEM_LOGTYPE_INFORMATION;
						str_severity = INFORMATION_TYPE;
						break;
					case EVENTLOG_WARNING_TYPE:
						severity = ITEM_LOGTYPE_WARNING;
						str_severity = WARNING_TYPE;
						break;
					case EVENTLOG_ERROR_TYPE:
						severity = ITEM_LOGTYPE_ERROR;
						str_severity = ERROR_TYPE;
						break;
					case EVENTLOG_AUDIT_FAILURE:
						severity = ITEM_LOGTYPE_FAILURE_AUDIT;
						str_severity = AUDIT_FAILURE;
						break;
					case EVENTLOG_AUDIT_SUCCESS:
						severity = ITEM_LOGTYPE_SUCCESS_AUDIT;
						str_severity = AUDIT_SUCCESS;
						break;
				}

				zbx_snprintf(str_logeventid, sizeof(str_logeventid), "%lu", logeventid);

				if (SUCCEED == regexp_match_ex(regexps, value, pattern, ZBX_CASE_SENSITIVE) &&
						SUCCEED == regexp_match_ex(regexps, str_severity, key_severity,
								ZBX_IGNORE_CASE) &&
						SUCCEED == regexp_match_ex(regexps, source, key_source,
								ZBX_IGNORE_CASE) &&
						SUCCEED == regexp_match_ex(regexps, str_logeventid, key_logeventid,
								ZBX_CASE_SENSITIVE))
				{
					send_err = process_value_cb(server, port, CONFIG_HOSTNAME, metric->key_orig,
							value, ITEM_STATE_NORMAL, &lastlogsize, NULL, &timestamp,
							source, &severity, &logeventid,
							metric->flags | ZBX_METRIC_FLAG_PERSISTENT);

					if (SUCCEED == send_err)
					{
						*lastlogsize_sent = lastlogsize;
						s_count++;
					}
				}
				p_count++;

				zbx_free(source);
				zbx_free(value);

				if (SUCCEED == send_err)
				{
					metric->lastlogsize = lastlogsize;
				}
				else
				{
					/* buffer is full, stop processing active checks */
					/* till the buffer is cleared */
					break;
				}

				/* do not flood Zabbix server if file grows too fast */
				if (s_count >= (rate * metric->refresh))
					break;

				/* do not flood local system if file grows too fast */
				if (p_count >= (4 * rate * metric->refresh))
					break;
			}

			pELR += ((PEVENTLOGRECORD)pELR)->Length;
		}

		if (pELR < pEndOfRecords)
			dwErr = ERROR_NO_MORE_ITEMS;
	}

finish:
	ret = SUCCEED;
out:
	zbx_close_eventlog(eventlog_handle);
	zbx_free(eventlog_name_w);
	zbx_free(pELRs);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}
