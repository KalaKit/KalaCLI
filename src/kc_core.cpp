//Copyright(C) 2026 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include "kc_core.hpp"

#if defined(KWIN_ANY)
#include <windows.h>
#include <conio.h>
#include <io.h>
#else
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <termios.h>
#include <csignal>
#include <cstdio>
#endif

#include "log_utils.hpp"
#include "math_utils.hpp"
#include "string_utils.hpp"

#include <fcntl.h>
#include <clocale>
#include <iostream>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>
#include <sstream>

using KalaHeaders::KalaLog::Log;
using KalaHeaders::KalaLog::LogType;
using KalaHeaders::KalaLog::TimeFormat;
using KalaHeaders::KalaLog::DateFormat;

using KalaHeaders::KalaMath::vec2;

using KalaHeaders::KalaString::SplitString;
using KalaHeaders::KalaString::IsStringInRange;

using KalaCLI::MAX_PAGE_LINES;
using KalaCLI::MAX_TYPED_TEXT_HISTORY;
using KalaCLI::COMMAND_PREFIX;
using KalaCLI::KalaCLICore;
using KalaCLI::Command;
using KalaCLI::PageDirection;

#ifdef __linux__
using std::raise;
#endif

using std::string;
using std::string_view;
using std::to_string;
using std::vector;
using std::array;
using std::pair;
using std::min;
using std::max;
using std::clamp;
using std::cout;
using std::cerr;
using std::flush;
using std::atexit;
using std::chrono::steady_clock;
using std::chrono::milliseconds;
using std::mutex;
using std::thread;
using std::lock_guard;
using std::atomic;
using std::function;
using std::ostringstream;

static constexpr u32 MAX_COMMAND_PARAM_LENGTH = 20;
static constexpr u32 PAGE_TITLE_MAX_WIDTH = 50;
static constexpr u32 WIDTH_MIN = 40;
static constexpr u32 HEIGHT_MIN = 15;

struct CoreData
{
	bool startedUpdate{};

	string lastFrame{};
	bool hasReservedLastFrame{};

	ostringstream frame{};
	bool hasReservedFrame{};

	bool inPageMode{};

	u32 cursorPos{};
	u32 innerW{};

	int real_out = -1;
	int real_err = -1;
	int cap_pipe[2] = { -1, -1 };
	mutex externalMutex{};

#ifdef KWIN_ANY
	DWORD g_origInMode{};
	DWORD g_origOutMode{};
	UINT g_origOutCP{};
	UINT g_origInCP{};
#else
	struct termios orig_term{};
#endif
};

struct OverwrittenRow
{
    string content{};
    u32 row{};
    PageDirection direction{};
};

struct PageViewBoxData
{
	atomic<bool> canConsoleWriteToPage{};
    bool isWrapped{};

	string pageTitle{};

    pair<u32, u32> pageScrollRange{};

	u32 innerPageH{};
	u32 pageTop{}; //absolute row at visible row 0
	u32 pageSelection{}; //absolute selected rendered row

	array<string, MAX_PAGE_LINES> pageContent{};
	u32 pageCount{}; //valid entries
	u32 pageHead{}; //oldest entry index

    //rendered content between pageTop and end of innerPageH
    vector<pair<string, u32>> renderedPageContent{};

    //extra storage for overwritten rows,
    //changes rendered page content
    vector<OverwrittenRow> overwrittenRows{};
};

struct InputBoxData
{
	u32 inputScrollOffset{};

	string typedText{};
	string nextFrameText{};

	array<string, MAX_TYPED_TEXT_HISTORY> typedTextHistory{};
	u32 typedTextCount{}; //valid entries
	u32 typedTextHead{}; //oldest entry index
	int typedHistoryPos = -1; //-1 = empty history, 0 = oldest, count - 1 = newest 
};

static CoreData coreData{};
static PageViewBoxData pageData{};
static InputBoxData inputData{};

static const string cmdHelp = string(COMMAND_PREFIX) + "help";
static const string cmdH = string(COMMAND_PREFIX) + "h";

static const string cmdClear = string(COMMAND_PREFIX) + "clear";
static const string cmdC = string(COMMAND_PREFIX) + "c";

static const string cmdCommand = string(COMMAND_PREFIX) + "command";
static const string cmdCmd = string(COMMAND_PREFIX) + "cmd";

static const string cmdEnableConsole = string(COMMAND_PREFIX) + "enableconsole";
static const string cmdEC = string(COMMAND_PREFIX) + "ec";

static const string cmdDisableConsole = string(COMMAND_PREFIX) + "disableconsole";
static const string cmdDC = string(COMMAND_PREFIX) + "dc";

static const string cmdSetPageTitle = string(COMMAND_PREFIX) + "setpagetitle";
static const string cmdSPT = string(COMMAND_PREFIX) + "spt";

static const string cmdCopyPage = string(COMMAND_PREFIX) + "copypage";
static const string cmdCP = string(COMMAND_PREFIX) + "cp";

static const string cmdExit = string(COMMAND_PREFIX) + "exit";
static const string cmdE = string(COMMAND_PREFIX) + "e";

static vector<Command> addedCommands{};

static function<void(u32, string&)> highlightedRowEnterAction{};
static function<void(string&)> prefixlessInputAction{};

enum class ALLOWED_KEY : u32
{
    KEY_BACKSPACE       = 8,
    KEY_DELETE          = 127,
    KEY_CARRIAGE_RETURN = 13, //\r, move cursor to start of line
    KEY_LINE_FEED       = 10, //\n, move cursor down one line
    KEY_TAB             = 9,
    KEY_ARROW_UP        = 1000,
    KEY_ARROW_DOWN      = 1001,
    KEY_ARROW_RIGHT     = 1002,
    KEY_ARROW_LEFT      = 1003
};

static void StartCapture()
{
    if (coreData.real_out != -1) return;

#if defined(KWIN_ANY)
    coreData.real_out = _dup(_fileno(stdout));
    coreData.real_err = _dup(_fileno(stderr));
    _pipe(
        coreData.cap_pipe,
        65536,
        _O_BINARY
        | _O_NOINHERIT);

    _dup2(coreData.cap_pipe[1], _fileno(stdout));
    _dup2(coreData.cap_pipe[1], _fileno(stderr));
    SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(coreData.cap_pipe[1]));
    SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(coreData.cap_pipe[1]));

    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    cout.setf(std::ios::unitbuf);
    cerr.setf(std::ios::unitbuf);

    thread([]
        {
            static thread_local string carry{};
            char buf[4096]{};

            while (true)
            {
                int n = _read(coreData.cap_pipe[0], buf, sizeof(buf) -1);
                if (n <= 0) continue;

                buf[n] = '\0';
                carry += buf;
                size_t pos{};

                while ((pos = carry.find('\n')) != string::npos)
                {
                    string line = carry.substr(0, pos);
                    carry.erase(0, pos + 1);
                    if (!line.empty()
                        && line.back() == '\r')
                    {
                        line.pop_back();
                    }

                    if (!pageData.canConsoleWriteToPage.load()) continue;

                    KalaCLICore::AppendToPage(std::move(line));
                }

                if (!carry.empty()
                    && pageData.canConsoleWriteToPage.load())
                {
                    string line = carry;
                    if (!line.empty()
                        && line.back() == '\r')
                    {
                        line.pop_back();
                    }
                    KalaCLICore::AppendToPage(std::move(line));
                    carry.clear();
                }
            }
        }).detach();
#else
    coreData.real_out = dup(STDOUT_FILENO);
    coreData.real_err = dup(STDERR_FILENO);
    pipe(coreData.cap_pipe);

    //don't let child keep read end
    fcntl(coreData.cap_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(coreData.cap_pipe[1], F_SETFD, FD_CLOEXEC);
    //make pipe big so big commands dont fill it and freeze
    fcntl(coreData.cap_pipe[1], F_SETPIPE_SZ, 1024 * 1024);

    dup2(coreData.cap_pipe[1], STDOUT_FILENO);
    dup2(coreData.cap_pipe[1], STDERR_FILENO);

    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    cout.setf(std::ios::unitbuf);
    cerr.setf(std::ios::unitbuf);

    thread([]
        {
            static thread_local string carry{};
            char buf[4096]{};

            while (true)
            {
                ssize_t n = read(coreData.cap_pipe[0], buf, sizeof(buf) -1);
                if (n <= 0) continue;

                buf[n] = '\0';
                carry += buf;
                size_t pos{};

                while ((pos = carry.find('\n')) != string::npos)
                {
                    string line = carry.substr(0, pos);
                    carry.erase(0, pos + 1);
                    if (!line.empty()
                        && line.back() == '\r')
                    {
                        line.pop_back();
                    }

                    if (!pageData.canConsoleWriteToPage.load()) continue;

                    KalaCLICore::AppendToPage(std::move(line));
                }

                if (!carry.empty()
                    && pageData.canConsoleWriteToPage.load())
                {
                    string line = carry;
                    if (!line.empty()
                        && line.back() == '\r')
                    {
                        line.pop_back();
                    }
                    KalaCLICore::AppendToPage(std::move(line));
                    carry.clear();
                }
            }
        }).detach();
#endif
}

static void StartDrawCapture(bool fullStart)
{
    if (fullStart)
    {
        fflush(stdout);
        fflush(stderr);
        cout.flush();
    }

#if defined(KWIN_ANY)
    _dup2(coreData.real_out, _fileno(stdout));
    _dup2(coreData.real_err, _fileno(stderr));
    SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(coreData.real_out));
    SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(coreData.real_err));
#else
    dup2(coreData.real_out, STDOUT_FILENO);
    dup2(coreData.real_err, STDERR_FILENO);
#endif
}

static void StopDrawCapture()
{
    #if defined(KWIN_ANY)
        _dup2(coreData.cap_pipe[1], _fileno(stdout));
        _dup2(coreData.cap_pipe[1], _fileno(stderr));
        SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(coreData.cap_pipe[1]));
        SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(coreData.cap_pipe[1]));
#else
        dup2(coreData.cap_pipe[1], STDOUT_FILENO);
        dup2(coreData.cap_pipe[1], STDERR_FILENO);
#endif

    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    cout.setf(std::ios::unitbuf);
    cerr.setf(std::ios::unitbuf);
}

static u32 GetPushedKey()
{
#if defined(KWIN_ANY)
    if (!_kbhit()) return 0;

    int ch = (u32)_getch();

    //esc sequence
    if (ch == 27)
    {
        if (!_kbhit()) return 27; //real ESC
        if (_getch() != '[') return 0;
        if (!_kbhit()) return 0;

        int ch3 = _getch();
        if (ch3 == 'A') return scast<u32>(ALLOWED_KEY::KEY_ARROW_UP);
        if (ch3 == 'B') return scast<u32>(ALLOWED_KEY::KEY_ARROW_DOWN);
        if (ch3 == 'C') return scast<u32>(ALLOWED_KEY::KEY_ARROW_RIGHT);
        if (ch3 == 'D') return scast<u32>(ALLOWED_KEY::KEY_ARROW_LEFT);

        return 0;
    }

    if (ch > 127) return 0;
    //allow every printable + allowed key
    if ((ch >= 32
        && ch <= 126)
        || ch == '\n'
        || ch == '\r'
        || ch == 8
        || ch == 127
        || ch == 9)
    {
        return scast<u32>(ch);
    }
    return 0;
#else
    fd_set fds{};

    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    timeval tv{ 0, 0 };

    if (select(
        STDIN_FILENO + 1,
        &fds,
        nullptr,
        nullptr,
        &tv) <= 0)
    {
        return 0;
    }

    unsigned char c{};
    if (read(
        STDIN_FILENO,
        &c,
        1) != 1)
    {
        return 0;
    }

    //esc sequence
    if (c == 27)
    {
        //peek for [
        unsigned char seq[2]{};

        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        timeval tv2{ 0, 10000 }; //10ms wait for sequence

        if (select(
            STDIN_FILENO + 1,
            &fds,
            nullptr,
            nullptr,
            &tv2) <= 0)
        {
            return 27;
        }

        if (read(
            STDIN_FILENO,
            &seq[0],
            1) != 1)
        {
            return 27;
        }
        if (seq[0] != '[') return 27; // not an arrow, drain rest

        if (read(
            STDIN_FILENO,
            &seq[1],
            1) != 1)
        {
            return 0;
        }

        if (seq[1] == 'A') return scast<u32>(ALLOWED_KEY::KEY_ARROW_UP);
        if (seq[1] == 'B') return scast<u32>(ALLOWED_KEY::KEY_ARROW_DOWN);
        if (seq[1] == 'C') return scast<u32>(ALLOWED_KEY::KEY_ARROW_RIGHT);
        if (seq[1] == 'D') return scast<u32>(ALLOWED_KEY::KEY_ARROW_LEFT);

        return 0; //other ESC seq, ignore
    }

    if (c > 127) return 0;
    //allow every printable + allowed key
    if ((c >= 32
        && c <= 126)
        || c == '\n'
        || c == '\r'
        || c == 8
        || c == 127
        || c == 9)
    {
        return scast<u32>(c);
    }

    return 0;
#endif
}

namespace KalaCLI
{
    int _ = atexit([]
        {
            if (coreData.startedUpdate)
            {
#if defined(KWIN_ANY)
                HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
                HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
                SetConsoleMode(hIn, coreData.g_origInMode);
                SetConsoleMode(hOut, coreData.g_origOutMode);
                SetConsoleOutputCP(coreData.g_origOutCP);
                SetConsoleCP(coreData.g_origInCP);
#else
                tcsetattr(
                    STDIN_FILENO,
                    TCSANOW,
                    &coreData.orig_term);
#endif
            }
        });

    u32 KalaCLICore::GetRowSize() { return coreData.innerW; }
    u32 KalaCLICore::GetRowCount() { return pageData.innerPageH; }
    u32 KalaCLICore::GetHighlightedRow(TextRange textRange)
    {
        if (!coreData.inPageMode) return 0;

        switch (textRange)
        {
        default:
        case TextRange::R_ALL:
            return coreData.cursorPos;
        case TextRange::R_VISIBLE:
            return coreData.cursorPos - pageData.pageTop;
        case TextRange::R_HIGHLIGHTED_ROW:
            return 0;
        }
    }

    bool KalaCLICore::CanConsoleWriteToPage() { return pageData.canConsoleWriteToPage.load(); }
    void KalaCLICore::SetConsoleWritesToPageState(bool state) { pageData.canConsoleWriteToPage.store(state); }

    bool KalaCLICore::GetWrapState() { return pageData.isWrapped; }
    void KalaCLICore::SetWrapState(bool state)
    {
        pageData.isWrapped = state;
        AppendToPage("Set wrapped mode to '" + string(pageData.isWrapped ? "true" : "false") + "'.");
    }

    void KalaCLICore::SetPageScrollRange(pair<u32, u32> range)
    {
        if (range.first + range.second >= pageData.innerPageH)
        {
            AppendToPage(
                "ERROR: Failed to set page scroll range because "
                "top + bottom range cannot be equal to or more than page height!");
            return;
        }

        pageData.pageScrollRange = range;

        AppendToPage(
            "Set page scroll range to '" 
            + to_string(pageData.pageScrollRange.first) + ", " 
            + to_string(pageData.pageScrollRange.second) + "'.");
    }

    void KalaCLICore::SetPageTitle(string_view title)
    {
        if (title.size() < 3)
        {
            AppendToPage("ERROR: Failed to update page title because it was too short!");
            return;
        }

        pageData.pageTitle = title;
    }

    vector<string> KalaCLICore::GetPageContent(TextRange textRange)
    {
        switch (textRange)
        {
        default:
        case TextRange::R_ALL:
        {
            lock_guard<mutex> lock(coreData.externalMutex);

            vector<string> content{};
            content.reserve(pageData.pageCount);

            for (u32 i = 0; i < pageData.pageCount; ++i)
            {
                content.push_back(pageData.pageContent[
                    (pageData.pageHead + i) % MAX_PAGE_LINES]);
            }

            return content;
        }
        case TextRange::R_VISIBLE:
        {
            vector<string> content{};
            content.reserve(pageData.renderedPageContent.size());
            for (const pair<string, u32>& row : pageData.renderedPageContent)
            {
                content.push_back(row.first);
            }

            return content;
        }
        case TextRange::R_HIGHLIGHTED_ROW:
        {
            return
            {
                pageData.renderedPageContent[
                    GetHighlightedRow(TextRange::R_VISIBLE)].first
            };
        }
        }
    }
    void KalaCLICore::SetPageContent(const vector<string>& content)
    {
        if (pageData.canConsoleWriteToPage.load())
        {
            AppendToPage("ERROR: Failed to update page content because console writing is enabled!");
            return;
        }
        if (content.size() > MAX_PAGE_LINES)
        {
            AppendToPage("ERROR: Failed to update page content because it was too big!");
            return;
        }

        lock_guard<mutex> lock(coreData.externalMutex);

        pageData.pageContent = {};
        pageData.pageHead = 0;
        pageData.pageCount = scast<u32>(content.size());
        pageData.pageTop = 0;
        pageData.pageSelection = 0;

        for (u32 i = 0; i < pageData.pageCount; ++i)
        {
            pageData.pageContent[i] = content[i];
        }
    }
    
    void KalaCLICore::AppendToPage(string_view line)
    {
        if (!coreData.startedUpdate)
        {
            Log::Print(line, true);
            return;
        }

        lock_guard<mutex> lock(coreData.externalMutex);
        if (pageData.pageCount < MAX_PAGE_LINES)
        {
            pageData.pageContent[(pageData.pageHead + pageData.pageCount) % MAX_PAGE_LINES] = line;
            ++pageData.pageCount;
        }
        else
        {
            pageData.pageContent[pageData.pageHead] = line;
            pageData.pageHead = (pageData.pageHead + 1) % MAX_PAGE_LINES;
        }
    }

    void KalaCLICore::OverwriteRow(
        const string& content,
        PageDirection pageDirection,
        u32 targetRow)
    {
        if (targetRow >= pageData.innerPageH)
        {
            AppendToPage("ERROR: Target row cannot exceed visible page area height!");
            return;
        }

        lock_guard<mutex> lock(coreData.externalMutex);

        for (auto it = pageData.overwrittenRows.begin();
            it != pageData.overwrittenRows.end();
            ++it)
        {
            if (it->row == targetRow
                && it->direction == pageDirection)
            {
                if (content.empty()) pageData.overwrittenRows.erase(it);
                else                 it->content = content;
                return;
            }
        }

        if (!content.empty())
        {
            pageData.overwrittenRows.push_back(
                {
                    .content = content,
                    .row = targetRow,
                    .direction = pageDirection
                });
        } 
    }

    void KalaCLICore::SetHighlightedRowEnterAction(function<void(u32, string&)> action)
    {
        AppendToPage("Updated highlighted enter action.");
        highlightedRowEnterAction = action;
    }

    void KalaCLICore::SetPrefixlessInputAction(function<void(string&)> action)
    {
        AppendToPage("Updated prefixless input action.");
        prefixlessInputAction = action;
    }

    void KalaCLICore::SendCommand(string_view command)
    {
        if (command.starts_with(COMMAND_PREFIX))
        {
            vector<string> split{};
            string err = SplitString(command, " ", split);
            if (!err.empty())
            {
                ForceClose(
                    "KalaCLI core error",
                    "Failed to send command '" + string(command) + "'! Reason: " + err);
            }

            string cmd = split[0];
            string cmdContent{};

            size_t pos = command.find(' ');
            if (pos != string_view::npos)
            {
                cmdContent = string(command.substr(pos + 1));
            }

            if (cmd == cmdHelp
                || cmd == cmdH)
            {
                if (split.size() > 1)
                {
                    AppendToPage("ERROR: 'help' command does not accept any arguments!");
                }
                else
                {
                    AppendToPage(
                        string(COMMAND_PREFIX) + "help, " + string(COMMAND_PREFIX) 
                        + "h: lists all available commands and what they do");
                    AppendToPage(
                        string(COMMAND_PREFIX) + "clear, " + string(COMMAND_PREFIX) 
                        + "c: clears all CLI page messages");
                    AppendToPage(
                        string(COMMAND_PREFIX) + "command command, " + string(COMMAND_PREFIX) 
                        + "cmd command: sends selected message as command to console");
                    AppendToPage(
                        string(COMMAND_PREFIX) + "enableconsole, " + string(COMMAND_PREFIX) 
                        + "ec: enables console-based updates");
                    AppendToPage(
                        string(COMMAND_PREFIX) + "disableconsole, " + string(COMMAND_PREFIX) 
                        + "dc: disables console-based updates");
                    AppendToPage(
                        string(COMMAND_PREFIX) + "copypage, " + string(COMMAND_PREFIX) 
                        + "cp: copies page content to clipboard");
                    AppendToPage(
                        string(COMMAND_PREFIX) + "setpagetitle title, " + string(COMMAND_PREFIX) 
                        + "spt title: updates page title");

                    for (const Command& thisCmd : addedCommands)
                    {
                        if (!thisCmd.targetFunction) continue; //ignore empty functions

                        AppendToPage(string(COMMAND_PREFIX) + thisCmd.primaryParam + ": " + thisCmd.description);
                    }
                }
            }
            else if (cmd == cmdClear
                || cmd == cmdC)
            {
                if (split.size() > 1)
                {
                    AppendToPage("ERROR: 'clear' command does not accept any arguments!");
                }
                else
                {
                    lock_guard<mutex> lock(coreData.externalMutex);

                    pageData.pageContent = {};
                    pageData.pageHead = 0;
                    pageData.pageCount = 0;
                    pageData.pageTop = 0;
                    pageData.pageSelection = 0;
                }
            }
            else if (cmd == cmdCommand
                || cmd == cmdCmd)
            {
                if (cmdContent.empty())
                {
                    AppendToPage("ERROR: 'command' command requires a command argument!");
                }
                else
                {
                    AppendToPage("  command: " + cmdContent);
                    string fullCmd = cmdContent + " 2>&1";

#if defined(KWIN_ANY)
                    FILE* fp = _popen(fullCmd.c_str(), "r");
#else
                    FILE* fp = popen(fullCmd.c_str(), "r");
#endif

                    if (!fp)
                    {
                        AppendToPage("ERROR: Failed to run command!");
                    }
                    else
                    {
                        char buf[4096];
                        string carry{};

                        while (fgets(buf, sizeof(buf), fp))
                        {
                            carry += buf;
                            size_t pos{};

                            while ((pos = carry.find('\n')) != string::npos)
                            {
                                string line = carry.substr(0, pos);
                                carry.erase(0, pos + 1);
                                if (!line.empty()
                                    && line.back() == '\r')
                                {
                                    line.pop_back();
                                }

                                AppendToPage(std::move(line));
                            }
                        }

                        if (!carry.empty())
                        {
                            if (carry.back() == '\r') carry.pop_back();
                            AppendToPage(std::move(carry));
                        }

#if defined(KWIN_ANY)
                        _pclose(fp);
#else
                        pclose(fp);
#endif
                    }
                }
            }
            else if (cmd == cmdEnableConsole
                || cmd == cmdEC)
            {
                if (split.size() > 1)
                {
                    AppendToPage("ERROR: 'enableconsole' command does not accept any arguments!");
                }
                else
                {
                    AppendToPage("Enabled console messages.");
                    pageData.canConsoleWriteToPage = true;
                }
            }
            else if (cmd == cmdDisableConsole
                || cmd == cmdDC)
            {
                if (split.size() > 1)
                {
                    AppendToPage("ERROR: 'disableconsole' command does not accept any arguments!");
                }
                else
                {
                    AppendToPage("Disabled console messages.");
                    pageData.canConsoleWriteToPage = false;
                }
            }
            else if (cmd == cmdSetPageTitle
                || cmd == cmdSPT)
            {
                if (cmdContent.empty())
                {
                    AppendToPage("ERROR: 'setpagetitle' command requires a value!");
                }
                else
                {
                    AppendToPage("Updated page title.");
                    SetPageTitle(cmdContent);
                }
            }
            else if (cmd == cmdCopyPage
                || cmd == cmdCP)
            {
                if (split.size() > 1)
                {
                    AppendToPage("ERROR: 'copypage' command does not accept any arguments!");
                }
                else
                {
                    string combined{};
                    size_t start = pageData.pageHead;
                    size_t end = min(pageData.pageContent.size(), start + pageData.pageCount);

#if defined(KWIN_ANY)
                    for (size_t i = start; i < end; ++i)
                    {
                        combined += pageData.pageContent[i];
                        if (i + 1 < end) combined += "\r\n";
                    }

                    if (combined.empty())
                    {
                        AppendToPage("ERROR: Failed to copy page content to clipboard because page content was empty!");
                    }
                    else
                    {
                        auto copy_to_clipboard = [&combined]() -> string
                            {
                                if (!OpenClipboard(nullptr)) return "Failed to run OpenClipboard.";
                                EmptyClipboard();

                                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, combined.size() + 1);
                                if (!hMem)
                                {
                                    CloseClipboard();
                                    return "Failed to allocate hMem.";
                                }

                                char* pMem = (char*)GlobalLock(hMem);
                                memcpy(pMem, combined.c_str(), combined.size() + 1);
                                GlobalUnlock(hMem);

                                SetClipboardData(CF_TEXT, hMem);
                                CloseClipboard();

                                return "";
                            };

                        string err = copy_to_clipboard();
                        if (!err.empty())
                        {
                            AppendToPage("ERROR: Failed to copy page content to clipboard! Reason: " + err);
                        }
                        else AppendToPage("Copied page content to clipboard.");
                    }
#else
                    for (size_t i = start; i < end; ++i)
                    {
                        combined += pageData.pageContent[i];
                        if (i + 1 < end) combined += "\n";
                    }

                    if (combined.empty())
                    {
                        AppendToPage("ERROR: Failed to copy page content to clipboard because page content was empty!");
                    }
                    else
                    {
                        auto copy_to_clipboard = [&combined](bool wl_copy) -> bool
                            {
                                FILE* pipe = popen(
                                    (wl_copy 
                                        ? "wl-copy 2>/dev/null" 
                                        : "xclip -selection clipboard 2>/dev/null"),
                                        "w");
                                if (!pipe) return false;

                                size_t written = fwrite(
                                    combined.data(),
                                    1,
                                    combined.size(),
                                    pipe);

                                return pclose(pipe) == 0
                                    && written == combined.size();
                            };

                        if (copy_to_clipboard(true)
                            || copy_to_clipboard(false))
                        {
                            AppendToPage("Copied page content to clipboard.");
                        }
                        else AppendToPage(
                            "ERROR: Failed to copy page content to clipboard because "
                            "'wl-copy' and 'xclip' failed to run!");
                    }
#endif
                }
            }
            else if (cmd == cmdExit
                || cmd == cmdE)
            {
                if (split.size() > 1)
                {
                    AppendToPage("ERROR: 'exit' command does not accept any arguments!");
                }
                else exit(0);
            }
            else
            {
                auto call_user_added_command = [&]() -> bool
                    {
                        for (const Command& thisCmd : addedCommands)
                        {
                            if (string(COMMAND_PREFIX) + thisCmd.primaryParam == cmd)
                            {
                                vector<string> splitCopy = split;
                                splitCopy.erase(splitCopy.begin());

                                thisCmd.targetFunction(splitCopy);
                                return true;
                            }
                        }

                        return false;
                    };

                if (!call_user_added_command())
                {
                    AppendToPage(
                        "ERROR: Command '" + cmd + "' was not found! "
                        "Type '/help' or '/h' to list all available commands.");
                }
            }
        }
        else
        {
            if (prefixlessInputAction) prefixlessInputAction(inputData.nextFrameText);
        }

        inputData.nextFrameText.clear();
    }

    void KalaCLICore::AddCommand(Command&& command)
    {
        if (!IsStringInRange(command.primaryParam, 1, MAX_COMMAND_PARAM_LENGTH))
        {
            AppendToPage(
				"ERROR: Failed to add command '" + string(command.primaryParam) 
				+ "' because its parameter length was out of range!");
            return;
        }

        if (command.primaryParam == cmdHelp
            || command.primaryParam == cmdH
            || command.primaryParam == cmdClear
            || command.primaryParam == cmdC
            || command.primaryParam == cmdCommand
            || command.primaryParam == cmdCmd
            || command.primaryParam == cmdEnableConsole
            || command.primaryParam == cmdEC
            || command.primaryParam == cmdDisableConsole
            || command.primaryParam == cmdDC
            || command.primaryParam == cmdSetPageTitle
            || command.primaryParam == cmdSPT
            || command.primaryParam == cmdCopyPage
            || command.primaryParam == cmdCP
            || command.primaryParam == cmdExit
            || command.primaryParam == cmdE)
        {
            AppendToPage(
                "ERROR: Failed to add command because its name '" 
                + string(command.primaryParam) + "' is used by a KalaCLI CLI command!");

            return;
        }

        for (size_t i = 0; i < addedCommands.size(); i++)
        {
			Command& c = addedCommands[i];

            if (command.primaryParam == c.primaryParam)
            {
				if (!command.targetFunction)
				{
					AppendToPage("Removed added command '" + string(command.primaryParam) + "'!");
					addedCommands.erase(addedCommands.begin() + i);
					return;
				}
				else
				{
					AppendToPage("WARNING: Overwrote command function for existing command '" + string(command.primaryParam) + "'!");
					c.targetFunction = command.targetFunction;
					return;
				}
            }
        }

        //you can clear target function for existing commands,
        //but you cannot pass empty function to newly added commands
        if (!command.targetFunction)
        {
            AppendToPage(
				"ERROR: Failed to add command '" + string(command.primaryParam) 
				+ "' because it had an empty target function!");
            return;
        }

        AppendToPage("Added new command '" + string(command.primaryParam) + "'!");
        addedCommands.push_back(std::move(command));
    }

    void KalaCLICore::UpdateDisplayedContent()
    {
        if (!coreData.startedUpdate)
        {
#if defined(KWIN_ANY)
            setlocale(LC_ALL, ".UTF8");

            HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

            coreData.g_origOutCP = GetConsoleOutputCP();
            coreData.g_origInCP = GetConsoleCP();
            GetConsoleMode(hIn, &coreData.g_origInMode);
            GetConsoleMode(hOut, &coreData.g_origOutMode);

            SetConsoleOutputCP(CP_UTF8);
            SetConsoleCP(CP_UTF8);

            //input: no line buffering, no echo, window events, vt input
            DWORD newIn = coreData.g_origInMode;
            newIn &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
            newIn |= ENABLE_VIRTUAL_TERMINAL_INPUT;
            SetConsoleMode(hIn, newIn);

            //output: enable vt sequences like ESC[24;2H
            DWORD newOut = coreData.g_origOutMode;
            newOut |= ENABLE_VIRTUAL_TERMINAL_PROCESSING
                | ENABLE_PROCESSED_OUTPUT
                | ENABLE_WRAP_AT_EOL_OUTPUT;
            //disable for better unicode borders
            newOut &= ~ENABLE_LVB_GRID_WORLDWIDE;
            SetConsoleMode(hOut, newOut);
#else
            setlocale(LC_ALL, "");

            tcgetattr(STDIN_FILENO, &coreData.orig_term);
            struct termios raw = coreData.orig_term;
            raw.c_lflag &= ~(ICANON | ECHO); //no line buffering, no kernel echo
            raw.c_cc[VMIN] = 0; //non-blocking read
            raw.c_cc[VTIME] = 0;
            tcsetattr(STDIN_FILENO, TCSANOW, &raw);
#endif

            StartCapture();
            coreData.startedUpdate = true;
        }

        auto get_console_size = []() -> vec2
            {
#if defined(KWIN_ANY)
                CONSOLE_SCREEN_BUFFER_INFO csbi{};

                HANDLE hReal = (HANDLE)_get_osfhandle(coreData.real_out);
                if (hReal != INVALID_HANDLE_VALUE
                    && hReal != NULL
                    && GetConsoleScreenBufferInfo(hReal, &csbi))
                {
                    return vec2
                    {
                        scast<f32>(csbi.srWindow.Right - csbi.srWindow.Left + 1),
                        scast<f32>(csbi.srWindow.Bottom - csbi.srWindow.Top + 1)
                    };
                }

                //use conout if stdout was piped
                HANDLE hCon = CreateFileW(
                    L"CONOUT$",
                    GENERIC_READ
                    | GENERIC_WRITE,
                    FILE_SHARE_READ
                    | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    0,
                    NULL);
                if (hCon != INVALID_HANDLE_VALUE)
                {
                    if (GetConsoleScreenBufferInfo(hCon, &csbi))
                    {
                        CloseHandle(hCon);
                        return vec2
                        {
                            scast<f32>(csbi.srWindow.Right - csbi.srWindow.Left + 1),
                            scast<f32>(csbi.srWindow.Bottom - csbi.srWindow.Top + 1)
                        };
                    }
                    CloseHandle(hCon);
                }

                return vec2{ 80.0f, 25.0f };
#else
                struct winsize ws{};
                if (ioctl(coreData.real_out, TIOCGWINSZ, &ws) == 0
                    && ws.ws_col > 0)
                {
                    return vec2
                    {
                        scast<f32>(ws.ws_col),
                        scast<f32>(ws.ws_row)
                    };
                }
                else return vec2{ 80.0f, 25.0f };
#endif
            };

        vec2 totalSize = get_console_size();

        if (totalSize.x < WIDTH_MIN
            || totalSize.y < HEIGHT_MIN)
        {
            //restore to real console before printing error
            StartDrawCapture(false);

            //clear
            cout << "\x1b[2J\x1b[H";
            //enable wrap for error
            cout << "\x1b[?7h";

            string target = 
                (totalSize.x < WIDTH_MIN
                && totalSize.y < HEIGHT_MIN)
                ? "width and height"
                : totalSize.x < WIDTH_MIN
                    ? "width"
                    : "height";

            cout << "Cannot draw, console " << target << " is too small!";
            cout.flush();

            //re-hijack before returning so next frame starts correctly
            StopDrawCapture();

            return;
        }

        if (!inputData.nextFrameText.empty()) SendCommand(inputData.nextFrameText);

        StartDrawCapture(true);

        auto count_wrapped_lines = [&](const string& src) -> int
            {
                if (src.empty()
                    || !pageData.isWrapped)
                {
                    return 1;
                }

                int c = 0;
                size_t start = 0;
                while (start < src.size())
                {
                    size_t remaining = src.size() - start;
                    if (remaining <= coreData.innerW)
                    { 
                        c++;
                        break;
                    }

                    size_t searchLimit = start + coreData.innerW;
                    size_t spacePos = src.rfind(' ', searchLimit);
                    if (spacePos != string::npos
                        && spacePos > start
                        && spacePos < searchLimit)
                    {
                        c++;
                        start = spacePos + 1;
                        while (start < src.size()
                            && src[start] == ' ')
                        {
                            ++start;
                        }
                    }
                    else
                    {
                        //the ... ends the src
                        if ((start + coreData.innerW) < src.size()
                            && coreData.innerW >= 3)
                        {
                            c++;
                            break;
                        }
                        else
                        {
                            c++;
                            start += coreData.innerW;
                        }
                    }
                }
                return c;
            };

        if (!coreData.hasReservedFrame)
        {
            coreData.frame.str().reserve(65536);
            coreData.hasReservedFrame = true;
        }

        auto draw_page_box = [&]() -> u32
            {
                u32 w = scast<u32>(totalSize.x);
                u32 h = scast<u32>(totalSize.y);

                u32 pageH = h - 3; //dont draw in bottom three rows
                coreData.innerW = w - 2;
                pageData.innerPageH = pageH - 2;

                string horizontalBar{};
                horizontalBar.reserve(coreData.innerW * 3);
                for (u32 i = 0; i < coreData.innerW; ++i) horizontalBar += "─";

                string spaces(coreData.innerW, ' ');

                //clear
                coreData.frame << "\x1b[2J\x1b[H";
                //disable auto-wrap
                coreData.frame << "\x1b[?7l";

                auto draw_top_border = [&]() -> void
                    {
                        if (pageData.pageTitle.empty()        //no content to draw
                            || pageData.pageTitle.size() < 3) //content is too short
                        {
                            coreData.frame << "┌" << horizontalBar << "┐";

                            return;
                        }

                        u32 available = min(w - 8, PAGE_TITLE_MAX_WIDTH);

                        auto get_display = [&]() -> string
                            {
                                static int offset{};
                                static int dir = 1; //1 = right, -1 = left
                                static int pause{};
                                static auto last = steady_clock::now();
                                static string lastTitle{};
                                static u32 lastAvailable{};

                                if (pageData.pageTitle != lastTitle
                                    || available != lastAvailable)
                                {
                                    lastTitle = pageData.pageTitle;
                                    lastAvailable = available;

                                    offset = 0;
                                    dir = 1;
                                    pause = 0;

                                    last = steady_clock::now();
                                }

                                if (pageData.pageTitle.size() <= available)
                                {
                                    offset = 0;
                                    dir = 1;
                                    return pageData.pageTitle;
                                }

                                int maxOffset = (int)pageData.pageTitle.size() - (int)available;
                                auto now = steady_clock::now();

                                if (now - last >= milliseconds(250))
                                {
                                    last = now;
                                    if (pause > 0) pause--;
                                    else
                                    {
                                        offset += dir;
                                        if (offset >= maxOffset)
                                        {
                                            offset = maxOffset; //far right - loses its ...
                                            dir = -1;
                                            pause = 1; //wait 1 sec
                                        }
                                        else if (offset <= 0)
                                        {
                                            offset = 0; //far left
                                            dir = 1;
                                            pause = 1; //wait 1 sec
                                        }
                                    }
                                }

                                if (offset < 0) offset = 0;
                                if (offset > maxOffset) offset = maxOffset;

                                //no dots at true left
                                if (offset == 0)
								{
									return pageData.pageTitle.substr(0, available - 3) + "...";
								}
                                //no dots at true right
                                else if (offset == maxOffset)
								{
									return "..." + pageData.pageTitle.substr(pageData.pageTitle.size() - (available - 3));
								}
                                //middle of scroll - dots on both sides
                                else
								{
									return "..." + pageData.pageTitle.substr(offset + 3, available - 6) + "...";
								}
                            };

                        string display = get_display();

                        u32 left = (coreData.innerW - (u32)display.size()) / 2;
                        u32 right = coreData.innerW - (u32)display.size() - left;

                        string lbar{}, rbar{};
                        lbar.reserve(left * 3);
                        rbar.reserve(right * 3);

                        for (u32 i = 0; i < left; ++i) lbar += "─";
                        for (u32 i = 0; i < right; ++i) rbar += "─";

                        coreData.frame << "┌" << lbar << display << rbar << "┐";
                    };

                draw_top_border();

                auto draw_middle = [&]() -> void
                    {
                        int total{};
                        {
                            lock_guard<mutex> lock(coreData.externalMutex);
                            for (u32 i = 0; i < pageData.pageCount; ++i)
                            {
                                total += count_wrapped_lines(pageData.pageContent[(pageData.pageHead + i) % MAX_PAGE_LINES]);
                            }
                        }

                        u32 topRange{};
                        u32 bottomRange{};

                        if (pageData.pageScrollRange.first < pageData.innerPageH
                            && pageData.pageScrollRange.second
                            < pageData.innerPageH - pageData.pageScrollRange.first)
                        {
                            topRange = pageData.pageScrollRange.first;
                            bottomRange = pageData.pageScrollRange.second;
                        }

                        u32 scrollHeight = pageData.innerPageH - topRange - bottomRange;
                        u32 maxTop = scast<u32>(max(0, total - (int)scrollHeight));

                        if (!coreData.inPageMode)
                        {
                            pageData.pageTop = maxTop;
                            pageData.pageSelection = max(0, total - 1);
                        }
                        else
                        {
                            pageData.pageTop = clamp(
								pageData.pageTop,
								0u,
								maxTop);
                            pageData.pageSelection = clamp(
								pageData.pageSelection,
								0u,
								scast<u32>(max(0, total - 1)));

                            if (pageData.pageSelection < pageData.pageTop)
                            {
                                pageData.pageTop = pageData.pageSelection;
                            }
                            if (pageData.pageSelection >= pageData.pageTop + scrollHeight)
                            {
                                pageData.pageTop = pageData.pageSelection - scrollHeight + 1;
                            }

                            pageData.pageTop = min(pageData.pageTop, maxTop);
                        }

                        pageData.renderedPageContent.clear();
                        pageData.renderedPageContent.reserve(scrollHeight);

                        u32 visibleStart = pageData.pageTop;
                        u32 visibleEnd = visibleStart + scrollHeight;

                        {
                            lock_guard<mutex> lock(coreData.externalMutex);
                            u32 absIdx{}; //absolute rendered index
                            for (u32 i = 0; i < pageData.pageCount && pageData.renderedPageContent.size() < scrollHeight; ++i)
                            {
                                const string& src = pageData.pageContent[(pageData.pageHead + i) % MAX_PAGE_LINES];
                                if (src.empty())
                                {
                                    if (absIdx >= visibleStart
                                        && absIdx < visibleEnd)
                                    {
                                        pageData.renderedPageContent.push_back({ "", i });    
                                    }
                                    absIdx++;
                                    continue;
                                }

                                //wrapping disabled: one page entry = one displayed row
                                if (!pageData.isWrapped)
                                {
                                    if (absIdx >= visibleStart
                                        && absIdx < visibleEnd)
                                    {
                                        pageData.renderedPageContent.push_back({ src, i }); 
                                    }

                                    absIdx++;
                                    continue;
                                }

                                size_t start{};
                                while (start < src.size()
                                    && pageData.renderedPageContent.size() < scrollHeight)
                                {
                                    string out{};
                                    size_t remaining = src.size() - start;
                                    if (remaining <= coreData.innerW)
                                    {
                                        out = src.substr(start);
                                        start = src.size();
                                    }
                                    else
                                    {
                                        size_t searchLimit = start + coreData.innerW;
                                        size_t spacePos = src.rfind(' ', searchLimit);
                                        if (spacePos != string::npos
                                            && spacePos > start
                                            && spacePos < searchLimit)
                                        {
                                            out = src.substr(start, spacePos - start);
                                            start = spacePos + 1;
                                            while (start < src.size()
                                                && src[start] == ' ')
                                            {
                                                ++start;
                                            }
                                        }
                                        else
                                        {
                                            string chunk = src.substr(start, coreData.innerW);
                                            bool hasMore = (start + coreData.innerW) < src.size();
                                            if (hasMore
                                                && coreData.innerW >= 3)
                                            {
                                                out = chunk.substr(0, coreData.innerW - 3) + "...";
                                                start = src.size();
                                            }
                                            else
                                            {
                                                out = chunk;
                                                start += coreData.innerW;
                                            }
                                        }
                                    }

                                    if (absIdx >= visibleStart
                                        && absIdx < visibleEnd)
                                    {
                                        pageData.renderedPageContent.push_back({ out, i }); 
                                    }
                                    absIdx++;
                                    if (absIdx >= visibleEnd
                                        && pageData.renderedPageContent.size() >= scrollHeight)
                                    {
                                        break;
                                    }
                                }

                                if (absIdx >= visibleEnd) break;
                            }
                        }

                        for (u32 i = 0; i < pageData.innerPageH; ++i)
                        {
                            bool inScrollRegion = i >= topRange
                                && i < pageData.innerPageH - bottomRange;

                            u32 contentIndex = inScrollRegion ? i - topRange : 0;

                            string line = (inScrollRegion
                                && contentIndex < pageData.renderedPageContent.size())
                                ? pageData.renderedPageContent[contentIndex].first
                                : "";

                            //apply persistent row overrides
                            {
                                lock_guard<mutex> lock(coreData.externalMutex);

                                for (const OverwrittenRow& row : pageData.overwrittenRows)
                                {
                                    //ignore rows outside the current visible area
                                    if (row.row >= pageData.innerPageH) continue;

                                    u32 targetRow = row.direction == PageDirection::D_UP
                                        ? pageData.innerPageH - 1 - row.row
                                        : row.row;

                                    if (targetRow == i) line = row.content;
                                }
                            }

                            if (line.size() < coreData.innerW)
                            {
                                line += string(coreData.innerW - line.size(), ' ');
                            }
                            else if (line.size() > coreData.innerW)
                            {
                                if (coreData.innerW >= 3) line = line.substr(0, coreData.innerW - 3) + "...";
                                else line = line.substr(0, coreData.innerW);
                            }

                            u32 absRow = pageData.pageTop + contentIndex;
                            if (coreData.inPageMode
                                && inScrollRegion
                                && absRow == pageData.pageSelection)
                            {
                                coreData.frame << "\n│\x1b[7m" << line << "\x1b[0m│";
                            }
                            else coreData.frame << "\n│" << line << "│";
                        }
                    };
                    
                draw_middle();

                //bottom border
                coreData.frame << "\n└" << horizontalBar << "┘";

                return pageH + 1;
            };

        u32 inputStartRow = draw_page_box();

        auto draw_input_box = [&]() -> void
            {
                string horizontalBar{};
                horizontalBar.reserve(coreData.innerW * 3);

                for (u32 i = 0; i < coreData.innerW; ++i) horizontalBar += "─";

				//enforce max input length
				if (inputData.typedText.size() > MAX_INPUT_LENGTH)
				{
					inputData.typedText.resize(MAX_INPUT_LENGTH);
				}

				//clamp scroll offset to valid range
				if (inputData.typedText.size() <= coreData.innerW) inputData.inputScrollOffset = 0;
				else
				{
					//keep cursor visible
					if (coreData.cursorPos < inputData.inputScrollOffset)
					{
						inputData.inputScrollOffset = coreData.cursorPos;
					}
					else if (coreData.cursorPos >= inputData.inputScrollOffset + coreData.innerW)
					{
						inputData.inputScrollOffset = coreData.cursorPos - coreData.innerW + 1;
					}

					//clamp max scroll
					u32 maxScroll = inputData.typedText.size() > coreData.innerW
						? inputData.typedText.size() - coreData.innerW + 1
						: 0;
					if (inputData.inputScrollOffset > maxScroll)
					{
						inputData.inputScrollOffset = maxScroll;
					}
				}

				//what is actually visible in input box
				string visibleText{};
				if (inputData.typedText.size() <= coreData.innerW) visibleText = inputData.typedText;
				else
				{
					visibleText = inputData.typedText.substr(inputData.inputScrollOffset, coreData.innerW);
				}

                u32 pad = coreData.innerW > visibleText.size()
                    ? coreData.innerW - visibleText.size()
                    : 0;

                //snap cursor to where page box stopped + wrap still disabled
                coreData.frame << "\x1b[" << inputStartRow << ";1H";

                //top border
                coreData.frame << "┌" << horizontalBar << "┐\n";
                //middle
                coreData.frame << "│" << visibleText << string(pad, ' ') << "│\n";
                //bottom border
                coreData.frame << "└" << horizontalBar << "┘";
                
                //snap cursor to input pos start
                coreData.frame << "\x1b[" << (inputStartRow + 1) << ";" << 2 << "H";

                //paste visible text + clear remainder
                coreData.frame << visibleText << string(pad, ' ');

                //move cursor to visible cursorPos
				u32 visibleCursor = (coreData.cursorPos >= inputData.inputScrollOffset)
					? coreData.cursorPos - inputData.inputScrollOffset
					: 0;
				if (visibleCursor > coreData.innerW) visibleCursor = coreData.innerW;

                coreData.frame << "\x1b[" << (inputStartRow + 1) << ";" << (2 + visibleCursor) << "H";

                //re-enable auto-wrap
                coreData.frame << "\x1b[?7h";
            };

        auto handle_input_char = [&](u32 c) -> void
            {
                ALLOWED_KEY key = scast<ALLOWED_KEY>(c);

                if (key == ALLOWED_KEY::KEY_TAB)
                {
                    coreData.inPageMode = !coreData.inPageMode;
                    return;
                }

                //
                // PAGE READ MODE
                //

                if (coreData.inPageMode)
                {
                    int total{};
                    {
                        lock_guard<mutex> lock(coreData.externalMutex);
                        for (u32 i = 0; i < pageData.pageCount; ++i)
                        {
                            total += count_wrapped_lines(pageData.pageContent[(pageData.pageHead + i) % MAX_PAGE_LINES]);
                        }
                    }

                    if (key == ALLOWED_KEY::KEY_ARROW_UP
                        && pageData.pageSelection > 0)
                    {
                        pageData.pageSelection--;
                    }
                    else if (key == ALLOWED_KEY::KEY_ARROW_DOWN
                        && (int)pageData.pageSelection < total - 1)
                    {
                        pageData.pageSelection++;
                    }
                    else if (key == ALLOWED_KEY::KEY_CARRIAGE_RETURN
                        || key == ALLOWED_KEY::KEY_LINE_FEED)
                    {
                        u32 highlightedRow = GetHighlightedRow(TextRange::R_VISIBLE);
                        string highlightedtext{};
                        {
                            lock_guard<mutex> lock(coreData.externalMutex);

                            if (highlightedRow < pageData.renderedPageContent.size())
                            {
                                highlightedtext = pageData.renderedPageContent[highlightedRow].first;
                            }
                        }

                        if (!highlightedRowEnterAction)
                        {
                            inputData.typedText = highlightedtext;
                            coreData.cursorPos = inputData.typedText.size();
                            coreData.inPageMode = false;
                        }
                        else
                        {
                            highlightedRowEnterAction(
                                highlightedRow,
                                highlightedtext);
                        }
                    }

                    return;
                }

                //
                // INPUT MODE 
                //

                //printable character
                if (!coreData.inPageMode
                    && c >= 32
                    && c <= 126)
                {
                    if (inputData.typedText.size() < MAX_INPUT_LENGTH)
                    {
                        inputData.typedText.insert(inputData.typedText.begin() + coreData.cursorPos, c);
                        coreData.cursorPos++;
                        inputData.typedHistoryPos = inputData.typedTextCount > 0
                            ? (int)inputData.typedTextCount - 1
                            : -1;
                    }

                    return;
                }

                if (key == ALLOWED_KEY::KEY_ARROW_LEFT
                    && coreData.cursorPos > 0)
                {
                    coreData.cursorPos--;
                }
                else if (key == ALLOWED_KEY::KEY_ARROW_RIGHT
                    && coreData.cursorPos < inputData.typedText.size())
                {
                    coreData.cursorPos++;
                }
                else if (key == ALLOWED_KEY::KEY_ARROW_UP)
                {
                    if (inputData.typedTextCount == 0) return;

                    if (inputData.typedText.empty()
                        || inputData.typedHistoryPos == -1)
                    {
                        inputData.typedHistoryPos = (int)inputData.typedTextCount - 1;
                    }
                    else
                    {
                        --inputData.typedHistoryPos;
                        if (inputData.typedHistoryPos < 0) inputData.typedHistoryPos = (int)inputData.typedTextCount - 1;
                    }

                    inputData.typedText = inputData.typedTextHistory[
						(inputData.typedTextHead + (u32)inputData.typedHistoryPos) 
						% MAX_TYPED_TEXT_HISTORY];
                    coreData.cursorPos = inputData.typedText.size();
                }
                else if (key == ALLOWED_KEY::KEY_ARROW_DOWN)
                {
                    if (inputData.typedTextCount == 0) return;

                    if (inputData.typedText.empty()
                        || inputData.typedHistoryPos == -1)
                    {
                        inputData.typedHistoryPos = 0;
                    }
                    else
                    {
                        ++inputData.typedHistoryPos;
                        if (inputData.typedHistoryPos >= (int)inputData.typedTextCount) inputData.typedHistoryPos = 0;
                    }

                    inputData.typedText = inputData.typedTextHistory[
						(inputData.typedTextHead + (u32)inputData.typedHistoryPos) 
						% MAX_TYPED_TEXT_HISTORY];
                    coreData.cursorPos = inputData.typedText.size();
                }
                else if (key == ALLOWED_KEY::KEY_CARRIAGE_RETURN
                    || key == ALLOWED_KEY::KEY_LINE_FEED)
                {
                    if (!inputData.typedText.empty())
                    {
                        inputData.nextFrameText = inputData.typedText;

                        if (inputData.typedTextCount < MAX_TYPED_TEXT_HISTORY)
                        {
                            inputData.typedTextHistory[
								(inputData.typedTextHead + inputData.typedTextCount) 
								% MAX_TYPED_TEXT_HISTORY] = inputData.typedText;
                            ++inputData.typedTextCount;
                        }
                        else
                        {
                            inputData.typedTextHistory[inputData.typedTextHead] = inputData.typedText;
                            inputData.typedTextHead = (inputData.typedTextHead + 1) % MAX_TYPED_TEXT_HISTORY;
                        }
                    }

                    inputData.typedText.clear();
                    coreData.cursorPos = 0;
					inputData.inputScrollOffset = 0;
                    inputData.typedHistoryPos = inputData.typedTextCount > 0
                        ? (int)inputData.typedTextCount - 1
                        : -1;
                }
                else if ((key == ALLOWED_KEY::KEY_BACKSPACE
                    || key == ALLOWED_KEY::KEY_DELETE)
                    && coreData.cursorPos > 0
                    && !inputData.typedText.empty())
                {
                    inputData.typedText.erase(coreData.cursorPos - 1, 1);
                    coreData.cursorPos--;
                    inputData.typedHistoryPos = inputData.typedTextCount > 0
                        ? (int)inputData.typedTextCount - 1
                        : -1;
                }
            };

        if (u32 c = GetPushedKey()) handle_input_char(c);

        draw_input_box();

        if (!coreData.hasReservedLastFrame)
        {
            coreData.lastFrame.reserve(65536);
            coreData.hasReservedLastFrame = true;
        }

        string thisFrame = std::move(coreData.frame).str();

        if (coreData.lastFrame != thisFrame)
        {
            cout << thisFrame << flush;
            coreData.lastFrame = std::move(thisFrame);
        }
        coreData.frame.str("");
        coreData.frame.clear();

        StopDrawCapture();
    }

	void KalaCLICore::ForceClose(
		const string& target,
		const string& reason)
	{
		Log::Print(
			"\n================"
			"\nFORCE CLOSE"
			"\n================\n",
			true);

		Log::Print(
			reason,
			target,
			LogType::LOG_ERROR,
			2,
			true,
			TimeFormat::TIME_NONE,
			DateFormat::DATE_NONE);

#ifdef _WIN32
		__debugbreak();
#else
		raise(SIGTRAP);
#endif

		abort();
	}
}
