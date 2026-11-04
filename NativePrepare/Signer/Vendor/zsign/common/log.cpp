#include "log.h"


thread_local string ZLog::captured;

int ZLog::g_nLogLevel = ZLog::E_INFO;

void ZLog::_Print(const char* szLog, int nColor)
{
#ifndef SATCHEL_SIGNER_QUIET
	captured += szLog;
#endif
}

void ZLog::Print(int nLevel, const char* szLog)
{
	if (g_nLogLevel >= nLevel) {
		_Print(szLog);
	}
}

void ZLog::PrintV(int nLevel, const char* szFormat, ...)
{
	if (g_nLogLevel >= nLevel) {
		FORMAT_V(szFormat, szLog);
		_Print(szLog);
	}
}

bool ZLog::Error(const char* szLog)
{
	_Print(szLog, 12);
	return false;
}

bool ZLog::ErrorV(const char* szFormat, ...)
{
	FORMAT_V(szFormat, szLog);
	_Print(szLog, 12);
	return false;
}

bool ZLog::Success(const char* szLog)
{
	_Print(szLog, 10);
	return true;
}

bool ZLog::SuccessV(const char* szFormat, ...)
{
	FORMAT_V(szFormat, szLog);
	_Print(szLog, 10);
	return true;
}

bool ZLog::PrintResult(bool bSuccess, const char* szLog)
{
	return bSuccess ? Success(szLog) : Error(szLog);
}

bool ZLog::PrintResultV(bool bSuccess, const char* szFormat, ...)
{
	FORMAT_V(szFormat, szLog);
	return bSuccess ? Success(szLog) : Error(szLog);
}

bool ZLog::Warn(const char* szLog)
{
	_Print(szLog, 6);
	return false;
}

bool ZLog::WarnV(const char* szFormat, ...)
{
	FORMAT_V(szFormat, szLog);
	_Print(szLog, 6);
	return false;
}

void ZLog::Print(const char* szLog)
{
	if (g_nLogLevel >= E_INFO) {
		_Print(szLog);
	}
}

void ZLog::PrintV(const char* szFormat, ...)
{
	if (g_nLogLevel >= E_INFO) {
		FORMAT_V(szFormat, szLog);
		_Print(szLog);
	}
}

void ZLog::Debug(const char* szLog)
{
	if (g_nLogLevel >= E_DEBUG) {
		_Print(szLog);
	}
}

void ZLog::DebugV(const char* szFormat, ...)
{
	if (g_nLogLevel >= E_DEBUG) {
		FORMAT_V(szFormat, szLog);
		_Print(szLog);
	}
}
