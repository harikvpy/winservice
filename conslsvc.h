/**
 * File         : TConsoleService.h
 * Author       : Hari
 * Purpose      : A class that wraps the Windows Service Program init/control/exit 
				  sequences. Also includes support for logging to a text file, which
				  is a common pattern for services.
 */
#pragma once

#include <windows.h>
#include <tchar.h>
#include <winsvc.h>
#include <stdarg.h>
#include <dbt.h>
#include <crtdbg.h>
#include "logfmwk.h"

/*
	TConsoleService is a class that encapsulates the logic of a 
	Windows service.

	To use, derive your own class from this class and override the
	run() method. Add the service initialization code to run() and
	after initialization is completed, call base class run() which
	will change status to STATUS_RUNNING.

	Once you have defined your derived class, declare an instance of
	the class as a global variable. Then from program main, call the
    start() method from this global instance object.
		
    Code to install/delete the service is left out to keep this as 
    light as possible. Event logging is also absent, though can be
    added easily.

    Also, by default the service only accepts STOP control command. If
    you want to support additional controls, add them to the 
    status_.dwControlsAccepted just before switching to SERVICE_RUNNING
    state. Note that dwControlsAccepted ought to be 0 during
    START_PENDING state.

	To support debugging, this class also checks for the comand line
	argument "/debug". If specified, it will bypass transferring the
	program control to SCM and instead execute the run() method directly.
	This provides an effective means to debug the service as a console
	program directly from Visual Studio, before running it as a service.

	Here's sample client code:

	class MyServicePrgram : public TConsoleService<FileLogger>
	{
		typedef TConsoleService baseClass;

	public:
		MyServicePrgram():
			: TConsoleService(L"myservice")
		{}
		virtual DWORD run()
		{
			// Do your own service initialization here
			// If initialiation is a lengthy process (>30 seconds), 
			// do it in a  worker thread which can be spawned here. 

			// Base class waits for the quit event.
			// If you want you can bypass this and have your own
			// wait routine which can also wait on other handles,
			// using, for example, WaitForMultipleObjects().
			DWORD dwRet = baseClass::run();

			// Do your service's deintialization here.
			// Again remember to use worker threads if this takes
			// more than 30 seconds.

			// a return value that will be propagated back to SCM
			return dwRet;
		}
	};

	// The TConsoleService class non-const static member
	TConsoleService* TConsoleService::s_pProgram = 0;

	// Declare an instance of the service program. Note that there 
	// can be only one instance of TConsoleService<> class per module!
	MyServicePrgram _service;

	// in the main function call the TConsoleService<>::start() method
	extern "C" int WINAPI _tmain( int argc, TCHAR* argv[] )
	{
		return _service.start();
	}
*/
template<class TLogger>
class TConsoleService {

	TConsoleService();
	TConsoleService(const TConsoleService&);
	TConsoleService& operator=(const TConsoleService&);

public:
	TConsoleService(const wchar_t* lpszServiceName)
		: hServiceStatus_(NULL)
		, hEventQuit_(NULL)
		, fDebugMode_(false)
		, logger_(getLogFilename(lpszServiceName).c_str())
	{
		::ZeroMemory((PVOID)&status_, sizeof(SERVICE_STATUS));
        ::ZeroMemory((PVOID)szServiceName_, sizeof(szServiceName_));

        ::wcscpy_s(szServiceName_, lpszServiceName);

        hEventQuit_ = ::CreateEvent(NULL, TRUE, FALSE, NULL);
        _ASSERTE(hEventQuit_ != NULL);

		status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
		status_.dwCurrentState = SERVICE_STOPPED;
		// SERVICE_ACCEPT_STOP will be added when switching from 
		// STOP_PENDING to RUNNING state.
		status_.dwControlsAccepted = 0;
		status_.dwWin32ExitCode = 0;
		status_.dwServiceSpecificExitCode = 0;
		status_.dwCheckPoint = 0;
		status_.dwWaitHint = 0;

		s_pProgram = this;
	}

	// start the service, to be called from _tmain
	DWORD start() throw()
	{
		// Parse commandline arguments to detect '/debug'. If specified, this
		// indicates the service to be run as a console program, usually for
		// debugging purposes.
		int nArgs = 0;
		LPWSTR* alpszArgs = ::CommandLineToArgvW(::GetCommandLineW(), &nArgs);
		for (int i=0; i<nArgs; i++) {
			LPWSTR lpszArg = alpszArgs[i];
			if (lpszArg[0] == L'/' || lpszArg[0] == L'-') {
				if (::_wcsicmp(&lpszArg[1], L"debug") == 0) {
					fDebugMode_ = true;
				}
			}
		}

		if (fDebugMode_) {
			// console program mode
			TConsoleService::_serviceMain((DWORD)nArgs, alpszArgs);
		} else {
			// service mode
			SERVICE_TABLE_ENTRYW st[] =
			{   // szServiceName_ is ignored for SERVICE_OWN_PROCESS, but we add it anyhow.
				{ szServiceName_, _serviceMain },
				{ NULL, NULL }
			};
			if (::StartServiceCtrlDispatcherW(st) == 0)
				status_.dwWin32ExitCode = GetLastError();
		}

		// don't forget to free the memory allocated by CommandLinetoArgvW()!
		::LocalFree(alpszArgs);

		return status_.dwWin32ExitCode;
	}

	void serviceMain(DWORD dwArgc, LPTSTR* lpszArgv)
	{
		// Register the control request handler
		status_.dwCurrentState = SERVICE_START_PENDING;
		if (isDebugMode()) {
			// Register a console Ctrl+Break handler
			::SetConsoleCtrlHandler(TConsoleService::_consoleCtrlHandler, TRUE);
			_putws(L"Press Ctrl+C or Ctrl+Break to quit...");
		} else {
			// register the service handler routine
			hServiceStatus_ = ::RegisterServiceCtrlHandlerEx(szServiceName_, (LPHANDLER_FUNCTION_EX)_serviceControlHandlerEx, (LPVOID)this);
			if (hServiceStatus_ == NULL) {
				//LogEvent(_T("Handler not installed"));
				return;
			}
		}
		setServiceStatus(SERVICE_START_PENDING);

		status_.dwWin32ExitCode = S_OK;
		status_.dwCheckPoint = 0;
		status_.dwWaitHint = 0;

		// When the Run function returns, the service has stopped.
		status_.dwWin32ExitCode = run();

		setServiceStatus(SERVICE_STOPPED);
	}

	// override this in your derived class to implement your own service's
	// initialization code.
	virtual DWORD run()
	{
		setServiceStatus(SERVICE_RUNNING);

		// Wait for the quit event to be signalled. Event would be signalled either from 
		// the SERVICE_STOP control handler or from the Ctrl+C control handler.
        ::WaitForSingleObject(hEventQuit_, INFINITE);

		return 0;
	}

	/* is '/debug' commandline option specified? */
	bool isDebugMode()
	{ return fDebugMode_; }

	Logger& getLogger()
	{ return logger_; }

	/* returns a std::wstring with the log file's fullname, including path */
	virtual std::wstring getLogFilename(const wchar_t* lpszServicename) const
	{
		wchar_t szTemp[MAX_PATH] = {0};
		::GetTempPathW(_countof(szTemp), szTemp);
		if (szTemp[::wcslen(szTemp)-1] != L'\\')
			::wcscat_s(szTemp, L"\\");
		return std::wstring(szTemp)+lpszServicename+L".log";
	}

	/**
	 * Various control command handlers, should be self-explanatory
	 */
	virtual void onStop() throw()
	{
		setServiceStatus(SERVICE_STOP_PENDING);
        ::SetEvent(hEventQuit_);
	}

	virtual void onPause() throw()
	{
	}

	virtual void onContinue() throw()
	{
	}

	virtual void onInterrogate() throw()
	{
	}

#if (_WIN32_WINNT >= 0x0600)
    virtual DWORD onPreShutdown(LPSERVICE_PRESHUTDOWN_INFO pInfo)
    {
        return NO_ERROR;
    }
#endif

	virtual void onShutdown() throw()
	{
	}

    virtual DWORD onDeviceEvent(DWORD dwDBT, PDEV_BROADCAST_HDR pHdr)
    {
        return NO_ERROR;
    }

    virtual DWORD onHardwareProfileChange(DWORD dwDBT)
    {
        return NO_ERROR;
    }

#if(_WIN32_WINNT >= 0x0501)
    virtual DWORD onSessionChange(DWORD dwEvent, PWTSSESSION_NOTIFICATION pSession)
    {
        return NO_ERROR;
    }
#endif

#if (_WIN32_WINNT >= 0x0502)
    virtual DWORD onPowerEvent(DWORD dwEvent, POWERBROADCAST_SETTING* pSetting)
    {
        return NO_ERROR;
    }
#endif
	virtual void onUnknownRequest(DWORD /*dwControl*/) throw()
	{
	}

	void setServiceStatus(DWORD dwState) throw()
	{
		status_.dwCurrentState = dwState;
		if (dwState == SERVICE_START_PENDING)
			status_.dwControlsAccepted = 0;
		else
            status_.dwControlsAccepted |= SERVICE_ACCEPT_STOP;

		if (!isDebugMode())
			::SetServiceStatus(hServiceStatus_, &status_);
	}

//Implementation
protected:
	static void WINAPI _serviceMain(DWORD dwArgc, LPWSTR* lpszArgv) throw()
	{
		s_pProgram->serviceMain(dwArgc, lpszArgv);
	}
	static DWORD WINAPI _serviceControlHandlerEx(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext) throw()
	{
		return reinterpret_cast<TConsoleService*>(lpContext)->serviceControlHandlerEx(dwControl, dwEventType, lpEventData); 
	}
	DWORD serviceControlHandlerEx(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData)
	{
        DWORD dwRet = NO_ERROR;

		switch (dwControl)
		{
		case SERVICE_CONTROL_STOP:
			onStop();
			break;
		case SERVICE_CONTROL_PAUSE:
			onPause();
			break;
		case SERVICE_CONTROL_CONTINUE:
			onContinue();
			break;
		case SERVICE_CONTROL_INTERROGATE:
			onInterrogate();
			break;
#if (_WIN32_WINNT >= 0x0600)
        case SERVICE_CONTROL_PRESHUTDOWN:
            dwRet = onPreShutdown((LPSERVICE_PRESHUTDOWN_INFO)lpEventData);
            break;
#endif
		case SERVICE_CONTROL_SHUTDOWN:
			onShutdown();
			break;
        case SERVICE_CONTROL_DEVICEEVENT:
            dwRet = onDeviceEvent(dwEventType, (PDEV_BROADCAST_HDR)lpEventData);
            break;
        case SERVICE_CONTROL_HARDWAREPROFILECHANGE:
            dwRet = onHardwareProfileChange(dwEventType);
            break;
#if (_WIN32_WINNT >= 0x0501)
        case SERVICE_CONTROL_SESSIONCHANGE:
            dwRet = onSessionChange(dwEventType, (PWTSSESSION_NOTIFICATION)lpEventData);
            break;
#endif
#if (_WIN32_WINNT >= 0x0502)
        case SERVICE_CONTROL_POWEREVENT:
            dwRet = onPowerEvent(dwEventType, (POWERBROADCAST_SETTING*)lpEventData);
            break;
#endif
		default:
			onUnknownRequest(dwControl);
		}
        return dwRet;
	}
	static BOOL WINAPI _consoleCtrlHandler(DWORD dwCtrlType)
	{
		return s_pProgram->consoleCtrlHandler(dwCtrlType);
	}
	BOOL consoleCtrlHandler(DWORD dwCtrlType)
	{
		if (dwCtrlType == CTRL_C_EVENT 
			|| dwCtrlType == CTRL_BREAK_EVENT 
			|| dwCtrlType == CTRL_SHUTDOWN_EVENT) {
			// thunk to the service's STOP control handler
			onStop();
			return TRUE;
		}
		return FALSE;
	}

protected:
	wchar_t szServiceName_[128];	// service name
	SERVICE_STATUS_HANDLE hServiceStatus_;	// status handle
    HANDLE hEventQuit_;		// event that signals service termination
	SERVICE_STATUS status_;	// service's current status
	bool fDebugMode_;			// set to true if /debug was specified
	TLogger logger_;	// default logger

	// Pointer to one and only instance of this class per program!
	// Remember to initialize this to NULL from your CPP file, or else
	// you'll get linker errors!
	static TConsoleService<TLogger>* s_pProgram;
};

template<class TLogger> TConsoleService<TLogger>* TConsoleService<TLogger>::s_pProgram = 0;
