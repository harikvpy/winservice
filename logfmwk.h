/**
 * File         : logfmwk.h
 * Author       : Hari
 * Purpose      : A simple logging framework
 *
 * We break logging into objects at two levels -- one for doing the actual 
 * logging (lower-level object) and another for the user to write messages 
 * to (higher-level object). One would typically use objects of the latter 
 * type to log messages and in a system there would be many instances of 
 * these. Each instance can be assigned a unique tag, which is then prefixed 
 * to all messages from that object.
 *
 * This mechanism will allow us to quickly filter messages from one source
 * for clarity.
 *
 * Log levels are set at the lower level object as it applies to the entire
 * system.
 *
 * Modification History:
 *
 * 12/7/11  Hari
 *      - Added stream interface to LogWriter
 */

#pragma once

#include <windows.h>
#include <time.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <io.h>

#define MAX_LOG_MESSAGE_LEN     4096
#define MAX_TAG_LEN             12

class Logger;   // 
class LogWriter;

/*
 * Logger class handles the actual logging of messages to an output medium.
 * Note that this class is primarily responsible for formatting the log messages
 * into a specific format. Writing the message to a destination medium is deferred
 * to the derived class through a virtual method.
 *
 * No magic here, just some tried and tested pattern for implementing an extensible
 * logging framework.
 */
class Logger {
    friend class TraceWriter;

	// Critical section wrapper
	class CriticalSection {
		CRITICAL_SECTION m_cs;
	public:
		CriticalSection() { ::InitializeCriticalSection(&m_cs); }
		~CriticalSection() { ::DeleteCriticalSection(&m_cs); }
		void Lock() { ::EnterCriticalSection(&m_cs); }
		void Unlock() { ::LeaveCriticalSection(&m_cs); }
	};
	// RAII class for a multithread syncing lock
	class AutoLock {
		CriticalSection& m_cs;
	public:
		AutoLock(CriticalSection& cs) : m_cs(cs) { m_cs.Lock(); }
		~AutoLock() { m_cs.Unlock(); }
	};
public:
	// predefined logging levels
	static const int LOG_LEVEL_ERROR=10;
	static const int LOG_LEVEL_WARNING=100;
	static const int LOG_LEVEL_INFORMATION=1000;
	static const int LOG_LEVEL_DEBUG=10000;
	static const int LOG_LEVEL_VERBOSE=100000;

    Logger()
		: sync_()
		, lastmsgtime_(0)
#ifdef _DEBUG
		, level_(LOG_LEVEL_DEBUG)
#else
		, level_(LOG_LEVEL_WARNING)
#endif
	{}
    virtual ~Logger()
	{}
	void write(int level, const wchar_t* tag, char const* msg)
	{
		// convert message to UTF-16
		wchar_t szMsg[MAX_LOG_MESSAGE_LEN+1] = {0};
		::MultiByteToWideChar(CP_THREAD_ACP,
			MB_PRECOMPOSED,
			msg,
			::strlen(msg),
			szMsg,
			_countof(szMsg)-1);
		writecomposed(level, tag, szMsg);
	}
	void write(int level, const wchar_t* tag, wchar_t const* msg)
	{
		writecomposed(level, tag, msg);
	}
	// get/set logging level
    void setLevel(int level)
    { level_ = level; }
	int getLevel()
    { return level_; }

protected:
	// fills the argument 1 with timestamp string (time is in local time)
	// make sure szStamp can hold at least 32 characters. That is, nChars >= 32.
	void getTimeStamp(wchar_t* szStamp, size_t nChars)
	{
		time_t now;
		now = ::time(NULL);
		struct tm lnow;
		::localtime_s(&lnow, &now);
		long zone = 0;
		::_get_timezone(&zone);
		long zonemins = zone/60; // convert to minutes
		zonemins = zonemins < 0 ? zonemins*-1 : zonemins;
		// write out date and time in the format YYYY/MM/DD HH:MM:SS
		::swprintf_s(szStamp, nChars, L"%04d/%02d/%02d %02d:%02d:%02d UTC%c%dmins",
			1900+lnow.tm_year,
			lnow.tm_mon,
			lnow.tm_mday,
			lnow.tm_hour,
			lnow.tm_min,
			lnow.tm_sec,
			zone >= 0 ? L'-' : L'+',
			zonemins);
	}
	// The heart of the logging system where messages get formatted 
	// before being sent to the derived class for writing to the output
	// medium.
	void writecomposed(int level, const wchar_t* tag, wchar_t const* msg)
	{
        if (level > getLevel())
            return;

		AutoLock l(sync_);

		time_t now;
		now = ::time(NULL);
		if (now != lastmsgtime_) {
			// last message was written at an earlier time
			// write out the current date time string
			wchar_t szTime[64] = {0};
			getTimeStamp(szTime, _countof(szTime));
			actualwrite(szTime);
			actualwrite(L"\r\n");
			lastmsgtime_ = now;
		}

		/*
		 * Now write the log message in the following format:
		 * <tag> <threadid> <message>
		 */
		wchar_t finalmsg[MAX_LOG_MESSAGE_LEN+MAX_TAG_LEN+7] = {0};
		::swprintf_s(finalmsg, _countof(finalmsg), L"%-12s %4d %s", 
			tag, ::GetCurrentThreadId(), msg);
		actualwrite(finalmsg);
	}

	// the actual log message writer -- derived classes should implement this
	// to write the message to the output medium.
    virtual void actualwrite(wchar_t const* msg) = 0;

private:
    CriticalSection sync_;	// for thread synchronization
    time_t lastmsgtime_;	// time when last message was written
    int level_;				// logging level, an iteger. meaning of different levels
							// to be decided by the class clients.
};

/*
 * The class that clients would typically use to write log messages to a 
 * specific logger. Key attributes of this class are:-
 *
 *		1. it allows messages to be tagged with a module keyword
 *		2. Provides C++ iostream interface to write messages thereby
 *		   eliminating the need for printf() format strings. This improves
 *		   code quality as mismatched format strings/args can resumt in
 *		   fatal runtime errors.
 *
 */
class LogWriter {
    LogWriter();
public:
    LogWriter(const wchar_t* lpszTag, Logger& logger) throw()
        : logger_(logger)
    {
		::wcscpy_s(szTag_, lpszTag);
    }
	// traditional printf like interfaces, for char and wchar_t
    void write(int level, char const* format, ...) throw()
    {
        va_list args;
        va_start(args, format);
        _write(level, format, args);
        va_end(args);
    }
    void write(int level, wchar_t const* format, ...) throw()
    {
        va_list args;
        va_start(args, format);
        _write(level, format, args);
        va_end(args);
    }

	// Provision for typesafe data output using C++ iostreams
private:
    template<class charT>
    class TSafeWriter : public std::basic_ostringstream<charT> {
        TSafeWriter() {}
    public:
        TSafeWriter(LogWriter* logger, int level) throw()
            : pWriter_(logger), level_(level)
        {}
        ~TSafeWriter()
        { pWriter_->write(level_, str().c_str()); }
        void setLogger(LogWriter* pLogger) throw()
        { pWriter_ = pLogger; }
    private:
        LogWriter* pWriter_;
        int level_;
    };

public:
    template<class charT>
    TSafeWriter<charT> getStream(int level)
    { return TSafeWriter<charT>(this, level); }
	// for unicode wide char strings
    TSafeWriter<wchar_t> getStreamW(int level)
    { return getStream<wchar_t>(level); }
	// for single byte strings
    TSafeWriter<char> getStreamA(int level)
    { return getStream<char>(level); }

protected:
    virtual void _write(int level, char const* format, va_list& args) throw()
    {
        char szMsg[MAX_LOG_MESSAGE_LEN] = {0};
        _vsnprintf_s(szMsg, _countof(szMsg)-1, _TRUNCATE, format, args);
		logger_.write(level, szTag_, szMsg);
    }
    virtual void _write(int level, wchar_t const* format, va_list& args) throw()
    {
        wchar_t szMsg[MAX_LOG_MESSAGE_LEN] = {0};
        _vsnwprintf_s(szMsg, _countof(szMsg)-1, _TRUNCATE, format, args);
        logger_.write(level, szTag_, szMsg);
    }
private:
    wchar_t szTag_[MAX_TAG_LEN+1];
    Logger& logger_;
};

/**
 * A logger to send log messages to nowhere.
 */
class NullLogger : public Logger {
public:
    NullLogger(wchar_t const* filename)
	{}
    virtual void actualwrite(wchar_t const* msg) throw(std::exception)
	{ // do nohting
	}
};

/*
 * Logger specialization for writing messages to a file.
 */
class FileLogger : public Logger {
	FileLogger();
	FileLogger(const FileLogger&);
	FileLogger& operator=(const FileLogger&);
public:
    FileLogger(wchar_t const* filename, bool fRollUp=true)
        : ofs_()
    {
        if (fRollUp && ::_waccess(filename, 0) != -1)
			rollover(filename);
        FILE* fp = 0;
        ::_wfopen_s(&fp, filename, L"a, ccs=UTF-16LE");
        if (fp) {
			// write out date and time in the format YYYY/MM/DD HH:MM:SS
			wchar_t szTime[64] = {0};
			getTimeStamp(szTime, _countof(szTime));
			fputws(szTime, fp);
			fputws(L" ######## BEGIN SESSION ########\r\n", fp);
            fclose(fp);
        }
        ofs_.open(filename, std::ios_base::app|std::ios_base::binary);
    }
    ~FileLogger()
    {
		wchar_t szTime[64] = {0};
		getTimeStamp(szTime, _countof(szTime));
		actualwrite(szTime);
		actualwrite(L" ######## END SESSION ########\r\n");
        ofs_.close();
    }
	/**
	 * Backs up a log file rollup backup to its next index value.
	 * That is, logfilename_1.log is backed up to logfilename_2.log.
	 */
	static void backup_rolledfile(const wchar_t* szPath, const wchar_t* szName, const wchar_t* szExt, int index)
	{
		wchar_t szRollFile[MAX_PATH]={0}, szRollFileBackup[MAX_PATH]={0};
		// now backup the roll up file to its next sequential name
		::swprintf_s(szRollFile, L"%s%s_%d%s", szPath, szName, index, szExt);
		::swprintf_s(szRollFileBackup, L"%s%s_%d%s", szPath, szName, index+1, szExt);
		if (::_waccess(szRollFile, 0) != -1) {
			// check if backup filename exists and if it does, recurse to to back it up
			if (::_waccess(szRollFileBackup, 0) != -1)
				backup_rolledfile(szPath, szName, szExt, index+1);
			::_wrename(szRollFile, szRollFileBackup);
		}
	}
	/*
	 * Rolls over existing logfiles to filename_n.log where n is a running
	   sequence number starting from 1.
	 */
	static void rollover(const wchar_t* filename)
	{
		wchar_t szDrive[_MAX_DRIVE]={0}, szDir[_MAX_DIR]={0}, szName[_MAX_FNAME]={0}, szExt[_MAX_EXT]={0};
		::_wsplitpath_s(filename, szDrive, szDir, szName, szExt);
		wchar_t szPath[MAX_PATH]={0};
		if (::wcslen(szDrive) || ::wcslen(szDir)) {
			::swprintf_s(szPath, L"%s%s", szDrive, szDir);
		} else {
			::_wgetcwd(szPath, _countof(szPath));
		}
		if (szPath[::wcslen(szPath)-1] != L'\\')
			::wcscat_s(szPath, L"\\");

		// backup any existing rollup files
		backup_rolledfile(szPath, szName, szExt, 1);

		// backup last log file
		wchar_t szRollFile[MAX_PATH]={0};
		::swprintf_s(szRollFile, L"%s%s_%d%s", szPath, szName, 1, szExt);
		::_wrename(filename, szRollFile);
	}
    virtual void actualwrite(wchar_t const* msg) throw(std::exception)
    {
        if (ofs_) {
            ofs_.write((const char*)msg, wcslen(msg)*sizeof(wchar_t));
            ofs_.flush();
        }
    }
private:
    std::ofstream ofs_;	// we have to treat the file as a byte stream
                        // so that we can write the BOM first and then
                        // the message (again as an array binary bytes)
};
