//Copyright(C) 2026 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include "kc_tui.hpp"

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
#endif

#include <fcntl.h>
#include <clocale>
#include <iostream>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>
#include <sstream>

#include "math_utils.hpp"
#include "string_utils.hpp"

#include "kc_cli.hpp"
#include "kc_core.hpp"

using KalaHeaders::KalaMath::vec2;

using KalaHeaders::KalaString::SplitString;

using KalaCLI::MAX_PAGE_LINES;
using KalaCLI::MAX_TYPED_TEXT_HISTORY;
using KalaCLI::KalaCLICore;
using KalaCLI::TUI_COMMAND_PREFIX;
using KalaCLI::Command;
using KalaCLI::TUI;

using std::string;
using std::string_view;
using std::vector;
using std::array;
using std::min;
using std::max;
using std::clamp;
using std::cout;
using std::cerr;
using std::flush;
using std::atexit;
using std::chrono::steady_clock;
using std::this_thread::sleep_for;
using std::chrono::milliseconds;
using std::mutex;
using std::thread;
using std::lock_guard;
using std::atomic;
using std::function;
using std::ostringstream;

static constexpr u32 PAGE_TITLE_MAX_WIDTH = 50;
static constexpr u32 WIDTH_MIN = 40;
static constexpr u32 HEIGHT_MIN = 15;

static bool startedUpdate{};

static string lastFrame{};
static bool hasReservedLastFrame{};

static ostringstream frame{};
static bool hasReservedFrame{};

static atomic<bool> canConsoleWriteToPage{};

static string pageTitle{};

static bool inPageMode{};
static u32 innerW{};
static u32 innerPageH{};
static u32 pageTop{}; //absolute wrapped line that is at row 0 of innerPageH
static u32 pageSelection{}; //absolute selected line (0 - totalWrapped - 1)

static array<string, MAX_PAGE_LINES> pageContent{};
static u32 pageCount{}; //valid entries
static u32 pageHead{}; //oldest entry index

static string typedText{};
static string nextFrameText{};
static array<string, MAX_TYPED_TEXT_HISTORY> typedTextHistory{};
static u32 typedTextCount{}; //valid entries
static u32 typedTextHead{}; //oldest entry index
static int typedHistoryPos = -1; //-1 = empty history, 0 = oldest, count - 1 = newest 

static u32 cursorPos{};

static int real_out = -1;
static int real_err = -1;
static int cap_pipe[2] = { -1, -1 };
static mutex externalMutex{};

static const string cmdHelp = string(TUI_COMMAND_PREFIX) + "help";
static const string cmdH = string(TUI_COMMAND_PREFIX) + "h";

static const string cmdClear = string(TUI_COMMAND_PREFIX) + "clear";
static const string cmdC = string(TUI_COMMAND_PREFIX) + "c";

static const string cmdCommand = string(TUI_COMMAND_PREFIX) + "command";
static const string cmdCmd = string(TUI_COMMAND_PREFIX) + "cmd";

static const string cmdEnableConsole = string(TUI_COMMAND_PREFIX) + "enableconsole";
static const string cmdEC = string(TUI_COMMAND_PREFIX) + "ec";

static const string cmdDisableConsole = string(TUI_COMMAND_PREFIX) + "disableconsole";
static const string cmdDC = string(TUI_COMMAND_PREFIX) + "dc";

static const string cmdSetPageTitle = string(TUI_COMMAND_PREFIX) + "setpagetitle";
static const string cmdSPT = string(TUI_COMMAND_PREFIX) + "spt";

static const string cmdExit = string(TUI_COMMAND_PREFIX) + "exit";
static const string cmdE = string(TUI_COMMAND_PREFIX) + "e";

static vector<Command> addedCommands{};
static function<void(string&)> prefixlessAction{};

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

#ifdef KWIN_ANY
static DWORD g_origInMode{};
static DWORD g_origOutMode{};
static UINT g_origOutCP{};
static UINT g_origInCP{};
#else
static struct termios orig_term{};
#endif

static void StartCapture()
{
    if (real_out != -1) return;

#if defined(KWIN_ANY)
    real_out = _dup(_fileno(stdout));
    real_err = _dup(_fileno(stderr));
    _pipe(
        cap_pipe,
        65536,
        _O_BINARY
        | _O_NOINHERIT);

    _dup2(cap_pipe[1], _fileno(stdout));
    _dup2(cap_pipe[1], _fileno(stderr));
    SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(cap_pipe[1]));
    SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(cap_pipe[1]));

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
                int n = _read(cap_pipe[0], buf, sizeof(buf) -1);
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

                    if (!canConsoleWriteToPage.load()) continue;

                    TUI::AppendToPage(std::move(line));
                }

                if (!carry.empty()
                    && canConsoleWriteToPage.load())
                {
                    string line = carry;
                    if (!line.empty()
                        && line.back() == '\r')
                    {
                        line.pop_back();
                    }
                    TUI::AppendToPage(std::move(line));
                    carry.clear();
                }
            }
        }).detach();
#else
    real_out = dup(STDOUT_FILENO);
    real_err = dup(STDERR_FILENO);
    pipe(cap_pipe);

    //don't let child keep read end
    fcntl(cap_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(cap_pipe[1], F_SETFD, FD_CLOEXEC);
    //make pipe big so big commands dont fill it and freeze
    fcntl(cap_pipe[1], F_SETPIPE_SZ, 1024 * 1024);

    dup2(cap_pipe[1], STDOUT_FILENO);
    dup2(cap_pipe[1], STDERR_FILENO);

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
                ssize_t n = read(cap_pipe[0], buf, sizeof(buf) -1);
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

                    if (!canConsoleWriteToPage.load()) continue;

                    TUI::AppendToPage(std::move(line));
                }

                if (!carry.empty()
                    && canConsoleWriteToPage.load())
                {
                    string line = carry;
                    if (!line.empty()
                        && line.back() == '\r')
                    {
                        line.pop_back();
                    }
                    TUI::AppendToPage(std::move(line));
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
    _dup2(real_out, _fileno(stdout));
    _dup2(real_err, _fileno(stderr));
    SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(real_out));
    SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(real_err));
#else
    dup2(real_out, STDOUT_FILENO);
    dup2(real_err, STDERR_FILENO);
#endif
}

static void StopDrawCapture()
{
    #if defined(KWIN_ANY)
        _dup2(cap_pipe[1], _fileno(stdout));
        _dup2(cap_pipe[1], _fileno(stderr));
        SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(cap_pipe[1]));
        SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(cap_pipe[1]));
#else
        dup2(cap_pipe[1], STDOUT_FILENO);
        dup2(cap_pipe[1], STDERR_FILENO);
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
            if (startedUpdate)
            {
#if defined(KWIN_ANY)
                HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
                HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
                SetConsoleMode(hIn, g_origInMode);
                SetConsoleMode(hOut, g_origOutMode);
                SetConsoleOutputCP(g_origOutCP);
                SetConsoleCP(g_origInCP);
#else
                tcsetattr(
                    STDIN_FILENO,
                    TCSANOW,
                    &orig_term);
#endif
            }
        });

    void TUI::Run()
    {
        if (CLI_COMMAND_PREFIX == TUI_COMMAND_PREFIX)
        {
            KalaCLICore::ForceClose(
                "KalaCLI TUI error",
                "Failed to run because cli and tui command prefixes are indentical!");
        }

        while(true)
        {
            TUI::UpdateDisplayedContent();

            sleep_for(milliseconds(16));
        }
    }

    bool TUI::CanConsoleWriteToPage() { return canConsoleWriteToPage.load(); }
    void TUI::SetConsoleWritesToPageState(bool state) { canConsoleWriteToPage.store(state); }

    void TUI::SetPageTitle(string_view title)
    {
        if (title.size() < 3)
        {
            AppendToPage("ERROR: Failed to update page title because it was too short!");
            return;
        }

        pageTitle = title;
    }

    void TUI::SetPageContent(const vector<string>& content)
    {
        if (content.size() > MAX_PAGE_LINES)
        {
            AppendToPage("ERROR: Failed to update page content because it was too big!");
            return;
        }
        if (canConsoleWriteToPage.load())
        {
            AppendToPage("ERROR: Failed to update page content because console writing is enabled!");
            return;
        }

        lock_guard<mutex> lock(externalMutex);

        pageContent = {};
        pageHead = 0;
        pageCount = scast<u32>(content.size());
        pageTop = 0;
        pageSelection = 0;

        for (u32 i = 0; i < pageCount; ++i)
        {
            pageContent[i] = content[i];
        }
    }
    void TUI::AppendToPage(string_view line)
    {
        lock_guard<mutex> lock(externalMutex);
        if (pageCount < MAX_PAGE_LINES)
        {
            pageContent[(pageHead + pageCount) % MAX_PAGE_LINES] = line;
            ++pageCount;
        }
        else
        {
            pageContent[pageHead] = line;
            pageHead = (pageHead + 1) % MAX_PAGE_LINES;
        }
    }

    void TUI::SendCommand(string_view command)
    {
        if (command.starts_with(TUI_COMMAND_PREFIX))
        {
            vector<string> split{};
            string err = SplitString(command, " ", split);
            if (!err.empty())
            {
                KalaCLICore::ForceClose(
                    "KalaCLI TUI error",
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
                        string(TUI_COMMAND_PREFIX) + "help, " + string(TUI_COMMAND_PREFIX) 
                        + "h: lists all available commands and what they do");
                    AppendToPage(
                        string(TUI_COMMAND_PREFIX) + "clear, " + string(TUI_COMMAND_PREFIX) 
                        + "c: clears all tui page messages");
                    AppendToPage(
                        string(TUI_COMMAND_PREFIX) + "command command, " + string(TUI_COMMAND_PREFIX) 
                        + "cmd command: sends selected message as command to console");
                    AppendToPage(
                        string(TUI_COMMAND_PREFIX) + "enableconsole, " + string(TUI_COMMAND_PREFIX) 
                        + "ec: enables console-based updates");
                    AppendToPage(
                        string(TUI_COMMAND_PREFIX) + "disableconsole, " + string(TUI_COMMAND_PREFIX) 
                        + "dc: disables console-based updates");
                    AppendToPage(
                        string(TUI_COMMAND_PREFIX) + "setpagetitle title, " + string(TUI_COMMAND_PREFIX) 
                        + "spt title: updates page title");

                    for (const Command& thisCmd : addedCommands)
                    {
                        if (!thisCmd.targetFunction) continue; //ignore empty functions

                        AppendToPage(string(TUI_COMMAND_PREFIX) + thisCmd.primaryParam + ": " + thisCmd.description);
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
                    lock_guard<mutex> lock(externalMutex);

                    pageContent = {};
                    pageHead = 0;
                    pageCount = 0;
                    pageTop = 0;
                    pageSelection = 0;
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
                    AppendToPage(cmdContent);
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
                    canConsoleWriteToPage = true;
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
                    canConsoleWriteToPage = false;
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
                            if (string(TUI_COMMAND_PREFIX) + thisCmd.primaryParam == cmd)
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
            if (!prefixlessAction) AppendToPage(nextFrameText);
            else prefixlessAction(nextFrameText);
        }

        nextFrameText.clear();
    }

    void TUI::AddCommand(Command&& command)
    {
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
            || command.primaryParam == cmdExit
            || command.primaryParam == cmdE)
        {
            AppendToPage(
                "ERROR: Failed to add command because its name '" 
                + string(command.primaryParam) + "' is used by a KalaCLI TUI command!");

            return;
        }

        for (Command& cmd : addedCommands)
        {
            if (command.primaryParam == cmd.primaryParam)
            {
                AppendToPage("WARNING: Overwrote command function for existing command '" + string(command.primaryParam) + "'!");
                cmd.targetFunction = command.targetFunction;
                return;
            }
        }

        //you can clear target function for existing commands,
        //but you cannot pass empty function to newly added commands
        if (!command.targetFunction)
        {
            AppendToPage("ERROR: Command '" + string(command.primaryParam) + "' has no target function!");
            return;
        }

        AppendToPage("Added new command '" + string(command.primaryParam) + "'!");
        addedCommands.push_back(std::move(command));
    }

    void TUI::SetPrefixlessTargetAction(function<void(string&)> action)
    {
        AppendToPage("Updated prefixless target action.");
        prefixlessAction = action;
    }

    void TUI::UpdateDisplayedContent()
    {
        if (!startedUpdate)
        {
#if defined(KWIN_ANY)
            setlocale(LC_ALL, ".UTF8");

            HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

            g_origOutCP = GetConsoleOutputCP();
            g_origInCP = GetConsoleCP();
            GetConsoleMode(hIn, &g_origInMode);
            GetConsoleMode(hOut, &g_origOutMode);

            SetConsoleOutputCP(CP_UTF8);
            SetConsoleCP(CP_UTF8);

            //input: no line buffering, no echo, window events, vt input
            DWORD newIn = g_origInMode;
            newIn &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
            newIn |= ENABLE_VIRTUAL_TERMINAL_INPUT;
            SetConsoleMode(hIn, newIn);

            //output: enable vt sequences like ESC[24;2H
            DWORD newOut = g_origOutMode;
            newOut |= ENABLE_VIRTUAL_TERMINAL_PROCESSING
                | ENABLE_PROCESSED_OUTPUT
                | ENABLE_WRAP_AT_EOL_OUTPUT;
            //disable for better unicode borders
            newOut &= ~ENABLE_LVB_GRID_WORLDWIDE;
            SetConsoleMode(hOut, newOut);
#else
            setlocale(LC_ALL, "");

            tcgetattr(STDIN_FILENO, &orig_term);
            struct termios raw = orig_term;
            raw.c_lflag &= ~(ICANON | ECHO); //no line buffering, no kernel echo
            raw.c_cc[VMIN] = 0; //non-blocking read
            raw.c_cc[VTIME] = 0;
            tcsetattr(STDIN_FILENO, TCSANOW, &raw);
#endif

            StartCapture();
            startedUpdate = true;
        }

        auto get_console_size = []() -> vec2
            {
#if defined(KWIN_ANY)
                CONSOLE_SCREEN_BUFFER_INFO csbi{};

                HANDLE hReal = (HANDLE)_get_osfhandle(real_out);
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
                if (ioctl(real_out, TIOCGWINSZ, &ws) == 0
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

        if (!nextFrameText.empty()) SendCommand(nextFrameText);

        StartDrawCapture(true);

        auto count_wrapped_lines = [&](const string& src) -> int
            {
                if (src.empty()) return 1;
                int c = 0;
                size_t start = 0;
                while (start < src.size())
                {
                    size_t remaining = src.size() - start;
                    if (remaining <= innerW)
                    { 
                        c++;
                        break;
                    }

                    size_t searchLimit = start + innerW;
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
                        if ((start + innerW) < src.size()
                            && innerW >= 3)
                        {
                            c++;
                            break;
                        }
                        else
                        {
                            c++;
                            start += innerW;
                        }
                    }
                }
                return c;
            };

        if (!hasReservedFrame)
        {
            frame.str().reserve(65536);
            hasReservedFrame = true;
        }

        auto draw_page_box = [&]() -> u32
            {
                u32 w = scast<u32>(totalSize.x);
                u32 h = scast<u32>(totalSize.y);

                u32 pageH = h - 3; //dont draw in bottom three rows
                innerW = w - 2;
                innerPageH = pageH - 2;

                string horizontalBar{};
                horizontalBar.reserve(innerW * 3);
                for (u32 i = 0; i < innerW; ++i) horizontalBar += "─";

                string spaces(innerW, ' ');

                //clear
                frame << "\x1b[2J\x1b[H";
                //disable auto-wrap
                frame << "\x1b[?7l";

                auto draw_top_border = [&]() -> void
                    {
                        if (pageTitle.empty()        //no content to draw
                            || pageTitle.size() < 3) //content is too short
                        {
                            frame << "┌" << horizontalBar << "┐";

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

                                if (pageTitle != lastTitle
                                    || available != lastAvailable)
                                {
                                    lastTitle = pageTitle;
                                    lastAvailable = available;

                                    offset = 0;
                                    dir = 1;
                                    pause = 0;

                                    last = steady_clock::now();
                                }

                                if (pageTitle.size() <= available)
                                {
                                    offset = 0;
                                    dir = 1;
                                    return pageTitle;
                                }

                                int maxOffset = (int)pageTitle.size() - (int)available;
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
                                if (offset == 0) return pageTitle.substr(0, available - 3) + "...";
                                //no dots at true right
                                else if (offset == maxOffset) return "..." + pageTitle.substr(pageTitle.size() - (available - 3));
                                //middle of scroll - dots on both sides
                                else return "..." + pageTitle.substr(offset + 3, available - 6) + "...";
                            };

                        string display = get_display();

                        u32 left = (innerW - (u32)display.size()) / 2;
                        u32 right = innerW - (u32)display.size() - left;

                        string lbar{}, rbar{};
                        lbar.reserve(left * 3);
                        rbar.reserve(right * 3);

                        for (u32 i = 0; i < left; ++i) lbar += "─";
                        for (u32 i = 0; i < right; ++i) rbar += "─";

                        frame << "┌" << lbar << display << rbar << "┐";
                    };

                draw_top_border();

                auto draw_middle = [&]() -> void
                    {
                        int total{};
                        {
                            lock_guard<mutex> lock(externalMutex);
                            for (u32 i = 0; i < pageCount; ++i)
                            {
                                total += count_wrapped_lines(pageContent[(pageHead + i) % MAX_PAGE_LINES]);
                            }
                        }

                        if (!inPageMode)
                        {
                            pageTop = max(0, total - (int)innerPageH);
                            pageSelection = max(0, total - 1);
                        }
                        else
                        {
                            pageTop = clamp(pageTop, 0u, scast<u32>(max(0, total - (int)innerPageH)));
                            pageSelection = clamp(pageSelection, 0u, scast<u32>(max(0, total - 1)));
                            if (pageSelection < pageTop) pageTop = pageSelection;
                            if (pageSelection >= pageTop + (int)innerPageH) pageTop = pageSelection - innerPageH + 1;
                        }

                        vector<string> wrapped{};
                        wrapped.reserve(innerPageH);

                        {
                            lock_guard<mutex> lock(externalMutex);
                            u32 absIdx{}; //absolute wrapped index
                            for (u32 i = 0; i < pageCount && wrapped.size() < innerPageH; ++i)
                            {
                                const string& src = pageContent[(pageHead + i) % MAX_PAGE_LINES];
                                if (src.empty())
                                {
                                    if (absIdx >= pageTop
                                        && absIdx < pageTop + innerPageH)
                                    {
                                        wrapped.push_back("");    
                                    }
                                    absIdx++;
                                    continue;
                                }

                                size_t start{};
                                while (start < src.size()
                                    && wrapped.size() < innerPageH)
                                {
                                    string out{};
                                    size_t remaining = src.size() - start;
                                    if (remaining <= innerW)
                                    {
                                        out = src.substr(start);
                                        start = src.size();
                                    }
                                    else
                                    {
                                        size_t searchLimit = start + innerW;
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
                                            string chunk = src.substr(start, innerW);
                                            bool hasMore = (start + innerW) < src.size();
                                            if (hasMore
                                                && innerW >= 3)
                                            {
                                                out = chunk.substr(0, innerW - 3) + "...";
                                                start = src.size();
                                            }
                                            else
                                            {
                                                out = chunk;
                                                start += innerW;
                                            }
                                        }
                                    }

                                    if (absIdx >= pageTop
                                        && absIdx < pageTop + innerPageH)
                                    {
                                        wrapped.push_back(out);
                                    }
                                    absIdx++;
                                    if (absIdx >= pageTop + innerPageH
                                        && wrapped.size() >= innerPageH)
                                    {
                                        break;
                                    }
                                }

                                if (absIdx >= pageTop + innerPageH) break;
                            }
                        }

                        for (u32 i = 0; i < innerPageH; ++i)
                        {
                            string line = (i < wrapped.size())
                                ? wrapped[i]
                                : "";

                            if (line.size() < innerW) line += string(innerW - line.size(), ' ');
                            else if (line.size() > innerW)
                            {
                                if (innerW >= 3) line = line.substr(0, innerW - 3) + "...";
                                else line = line.substr(0, innerW);
                            }

                            u32 absRow = pageTop + i;
                            if (inPageMode
                                && absRow == pageSelection)
                            {
                                frame << "\n│\x1b[7m" << line << "\x1b[0m│";
                            }
                            else frame << "\n│" << line << "│";
                        }
                    };
                    
                draw_middle();

                //bottom border
                frame << "\n└" << horizontalBar << "┘";

                return pageH + 1;
            };

        u32 inputStartRow = draw_page_box();

        auto draw_input_box = [&]() -> void
            {
                string horizontalBar{};
                horizontalBar.reserve(innerW * 3);

                for (u32 i = 0; i < innerW; ++i) horizontalBar += "─";

                u32 pad = innerW > typedText.size()
                    ? innerW - typedText.size()
                    : 0;

                //snap cursor to where page box stopped + wrap still disabled
                frame << "\x1b[" << inputStartRow << ";1H";

                //top border
                frame << "┌" << horizontalBar << "┐\n";
                //middle
                frame << "│" << typedText << string(pad, ' ') << "│\n";
                //bottom border
                frame << "└" << horizontalBar << "┘";
                
                //snap cursor to input pos start
                frame << "\x1b[" << (inputStartRow + 1) << ";" << 2 << "H";

                //paste whole input text string + clear remainder
                frame << typedText << string(pad, ' ');

                //move cursor to cursorPos
                frame << "\x1b[" << (inputStartRow + 1) << ";" << (2 + cursorPos) << "H";

                //re-enable auto-wrap
                frame << "\x1b[?7h";
            };

        auto handle_input_char = [&](u32 c) -> void
            {
                u32 innerW = scast<u32>(totalSize.x) - 2;

                ALLOWED_KEY key = scast<ALLOWED_KEY>(c);

                if (key == ALLOWED_KEY::KEY_TAB)
                {
                    inPageMode = !inPageMode;
                    return;
                }

                //
                // PAGE READ MODE
                //

                if (inPageMode)
                {
                    int total{};
                    {
                        lock_guard<mutex> lock(externalMutex);
                        for (u32 i = 0; i < pageCount; ++i)
                        {
                            total += count_wrapped_lines(pageContent[(pageHead + i) % MAX_PAGE_LINES]);
                        }
                    }

                    if (key == ALLOWED_KEY::KEY_ARROW_UP
                        && pageSelection > 0)
                    {
                        pageSelection--;
                        if (pageSelection < pageTop) pageTop = pageSelection;
                    }
                    else if (key == ALLOWED_KEY::KEY_ARROW_DOWN)
                    {
                        if ((int)pageSelection < total - 1)
                        {
                            pageSelection++;
                            if (pageSelection >= pageTop + innerPageH) pageTop++;
                        }
                    }
                    else if (key == ALLOWED_KEY::KEY_CARRIAGE_RETURN
                        || key == ALLOWED_KEY::KEY_LINE_FEED)
                    {
                        string toCopy{};
                        {
                            lock_guard<mutex> lock(externalMutex);
                            u32 absIdx{}; //absolute wrapped index
                            for (u32 i = 0; i < pageCount; ++i)
                            {
                                const string& src = pageContent[(pageHead + i) % MAX_PAGE_LINES];
                                if (src.empty())
                                {
                                    if (absIdx == pageSelection)
                                    {
                                        toCopy = "";
                                        break;    
                                    }
                                    absIdx++;
                                    continue;
                                }

                                size_t start{};
                                while (start < src.size())
                                {
                                    string out{};
                                    size_t remaining = src.size() - start;
                                    if (remaining <= innerW)
                                    {
                                        out = src.substr(start);
                                        start = src.size();
                                    }
                                    else
                                    {
                                        size_t searchLimit = start + innerW;
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
                                            string chunk = src.substr(start, innerW);
                                            bool hasMore = (start + innerW) < src.size();
                                            if (hasMore
                                                && innerW >= 3)
                                            {
                                                out = chunk.substr(0, innerW - 3) + "...";
                                                start = src.size();
                                            }
                                            else
                                            {
                                                out = chunk;
                                                start += innerW;
                                            }
                                        }
                                    }

                                    if (absIdx == pageSelection)
                                    {
                                        toCopy = out;
                                        break;
                                    }
                                    absIdx++;
                                }

                                if (absIdx >= pageSelection) break;
                            }
                        }

                        typedText = toCopy;
                        cursorPos = typedText.size();
                        inPageMode = false;
                    }

                    return;
                }

                //
                // INPUT MODE 
                //

                //printable character
                if (!inPageMode
                    && c >= 32
                    && c <= 126)
                {
                    if (typedText.size() < innerW)
                    {
                        typedText.insert(typedText.begin() + cursorPos, c);
                        cursorPos++;
                        typedHistoryPos = typedTextCount > 0
                            ? (int)typedTextCount - 1
                            : -1;
                    }

                    return;
                }

                if (key == ALLOWED_KEY::KEY_ARROW_LEFT
                    && cursorPos > 0)
                {
                    cursorPos--;
                }
                else if (key == ALLOWED_KEY::KEY_ARROW_RIGHT
                    && cursorPos < typedText.size())
                {
                    cursorPos++;
                }
                else if (key == ALLOWED_KEY::KEY_ARROW_UP)
                {
                    if (typedTextCount == 0) return;

                    if (typedText.empty()
                        || typedHistoryPos == -1)
                    {
                        typedHistoryPos = (int)typedTextCount - 1;
                    }
                    else
                    {
                        --typedHistoryPos;
                        if (typedHistoryPos < 0) typedHistoryPos = (int)typedTextCount - 1;
                    }

                    typedText = typedTextHistory[(typedTextHead + (u32)typedHistoryPos) % MAX_TYPED_TEXT_HISTORY];
                    if (typedText.size() > innerW) typedText.resize(innerW); //hard cut, no ...
                    cursorPos = typedText.size();
                }
                else if (key == ALLOWED_KEY::KEY_ARROW_DOWN)
                {
                    if (typedTextCount == 0) return;

                    if (typedText.empty()
                        || typedHistoryPos == -1)
                    {
                        typedHistoryPos = 0;
                    }
                    else
                    {
                        ++typedHistoryPos;
                        if (typedHistoryPos >= (int)typedTextCount) typedHistoryPos = 0;
                    }

                    typedText = typedTextHistory[(typedTextHead + (u32)typedHistoryPos) % MAX_TYPED_TEXT_HISTORY];
                    if (typedText.size() > innerW) typedText.resize(innerW); //hard cut, no ...
                    cursorPos = typedText.size();
                }
                else if (key == ALLOWED_KEY::KEY_CARRIAGE_RETURN
                    || key == ALLOWED_KEY::KEY_LINE_FEED)
                {
                    if (!typedText.empty())
                    {
                        nextFrameText = typedText;

                        if (typedTextCount < MAX_TYPED_TEXT_HISTORY)
                        {
                            typedTextHistory[(typedTextHead + typedTextCount) % MAX_TYPED_TEXT_HISTORY] = typedText;
                            ++typedTextCount;
                        }
                        else
                        {
                            typedTextHistory[typedTextCount] = typedText;
                            typedTextHead = (typedTextHead + 1) % MAX_TYPED_TEXT_HISTORY;
                        }
                    }

                    typedText.clear();
                    cursorPos = 0;
                    typedHistoryPos = typedTextCount > 0
                        ? (int)typedTextCount - 1
                        : -1;
                }
                else if ((key == ALLOWED_KEY::KEY_BACKSPACE
                    || key == ALLOWED_KEY::KEY_DELETE)
                    && cursorPos > 0
                    && !typedText.empty())
                {
                    typedText.erase(cursorPos - 1, 1);
                    cursorPos--;
                    typedHistoryPos = typedTextCount > 0
                        ? (int)typedTextCount - 1
                        : -1;
                }
            };

        if (u32 c = GetPushedKey()) handle_input_char(c);

        draw_input_box();

        if (!hasReservedLastFrame)
        {
            lastFrame.reserve(65536);
            hasReservedLastFrame = true;
        }

        string thisFrame = std::move(frame).str();

        if (lastFrame != thisFrame)
        {
            cout << thisFrame << flush;
            lastFrame = std::move(thisFrame);
        }
        frame.str("");
        frame.clear();

        StopDrawCapture();
    }
}
