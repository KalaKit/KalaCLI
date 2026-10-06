//Copyright(C) 2026 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include "kc_tui.hpp"

#if defined(KWIN_ANY)
#include <windows.h>
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#else
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <termios.h>
#endif

#include <clocale>
#include <iostream>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>

#include "math_utils.hpp"

using KalaHeaders::KalaMath::vec2;

using KalaCLI::MAX_PAGE_LINES;
using KalaCLI::MAX_TYPED_TEXT_HISTORY;

using std::string;
using std::vector;
using std::array;
using std::min;
using std::cout;
using std::atexit;
using std::chrono::steady_clock;
using std::this_thread::sleep_for;
using std::chrono::milliseconds;
using std::mutex;
using std::thread;
using std::lock_guard;
using std::atomic;

static constexpr u32 PAGE_TITLE_MAX_WIDTH = 50;
static constexpr u32 WIDTH_MIN = 40;
static constexpr u32 HEIGHT_MIN = 15;

static bool startedUpdate{};

static bool isEnabled{};
static atomic<bool> canConsoleWriteToPage{};

static string pageTitle{};

static array<string, MAX_PAGE_LINES> pageContent{};
static u32 pageCount{}; //valid entries
static u32 pageHead{}; //oldest entry index

static string typedText{};
static array<string, MAX_TYPED_TEXT_HISTORY> typedTextHistory{};
static u32 typedTextCount{}; //valid entries
static u32 typedTextHead{}; //oldest entry index
static int typedHistoryPos = -1; //-1 = empty history, 0 = oldest, count - 1 = newest 

static u32 cursorPos{};

static int real_out = -1;
static int real_err = -1;
static int cap_pipe[2] = { -1, -1 };
static mutex externalMutex{};

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

#ifdef KLIN_ANY
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
        4096,
        _O_BINARY
        | _O_NOINHERIT);

    _dup2(cap_pipe[1], _fileno(stdout));
    _dup2(cap_pipe[1], _fileno(stderr));
    SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(cap_pipe[1]));
    SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(cap_pipe[1]));

    thread([]
        {
            static thread_local string carry{};
            char buf[4096]{};

            while (true)
            {
                int n = _read(cap_pipe[0], buf, sizeof(buf) -1);
                if (n <= 0)
                {
                    sleep_for(milliseconds(5));
                    continue;
                }

                buf[n] = '\0';
                carry += buf;
                size_t pos{};

                while ((pos = carry.find('\n')) != string::npos)
                {
                    if (!canConsoleWriteToPage.load()) break;

                    string line = carry.substr(0, pos);
                    carry.erase(0, pos + 1);

                    lock_guard<mutex> lock(externalMutex);
                    if (pageCount < MAX_PAGE_LINES)
                    {
                        pageContent[(pageHead + pageCount) % MAX_PAGE_LINES] = std::move(line);
                        ++pageCount;
                    }
                    else
                    {
                        pageContent[pageHead] = std::move(line);
                        pageHead = (pageHead + 1) % MAX_PAGE_LINES;
                    }
                }
            }
        }).detach();
#else
    real_out = dup(STDOUT_FILENO);
    real_err = dup(STDERR_FILENO);
    pipe(cap_pipe);

    dup2(cap_pipe[1], STDOUT_FILENO);
    dup2(cap_pipe[1], STDERR_FILENO);

    thread([]
        {
            static thread_local string carry{};
            char buf[4096]{};

            while (true)
            {
                ssize_t n = read(cap_pipe[0], buf, sizeof(buf) -1);
                if (n <= 0)
                {
                    sleep_for(milliseconds(5));
                    continue;
                }

                buf[n] = '\0';
                carry += buf;
                size_t pos{};

                while ((pos = carry.find('\n')) != string::npos)
                {
                    if (!canConsoleWriteToPage.load()) break;

                    string line = carry.substr(0, pos);
                    carry.erase(0, pos + 1);

                    lock_guard<mutex> lock(externalMutex);
                    if (pageCount < MAX_PAGE_LINES)
                    {
                        pageContent[(pageHead + pageCount) % MAX_PAGE_LINES] = std::move(line);
                        ++pageCount;
                    }
                    else
                    {
                        pageContent[pageHead] = std::move(line);
                        pageHead = (pageHead + 1) % MAX_PAGE_LINES;
                    }
                }
            }
        }).detach();
#endif
}

static u32 GetPushedKey()
{
#if defined(KWIN_ANY)
    if (!_kbhit()) return 0;

    int ch = (u32)_getch();

    //system key
    if (ch == 0
        || ch == 224)
    {
        int ch2 = _getch();

        if (ch2 == 72) return scast<u32>(ALLOWED_KEY::KEY_ARROW_UP);
        if (ch2 == 80) return scast<u32>(ALLOWED_KEY::KEY_ARROW_DOWN);
        if (ch2 == 77) return scast<u32>(ALLOWED_KEY::KEY_ARROW_RIGHT);
        if (ch2 == 75) return scast<u32>(ALLOWED_KEY::KEY_ARROW_LEFT);

        return 0;
    }

    return scast<u32>(ch);
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
    

#if defined(KLIN_ANY)
    int _ = atexit([]
    { 
        if (startedUpdate)
        {
            tcsetattr(
                STDIN_FILENO,
                TCSANOW,
                &orig_term);
        }
    });
#endif

    bool TUI::IsEnabled() { return isEnabled; }
    void TUI::SetEnabledState(bool state) { isEnabled = state; }

    bool TUI::CanConsoleWriteToPage() { return canConsoleWriteToPage.load(); }
    void TUI::SetConsoleWritesToPageState(bool state) { canConsoleWriteToPage.store(state); }

    void TUI::SetPageTitle(string_view title)
    {
        if (title.size() < 3) return;

        pageTitle = title;
    }

    void TUI::SetPageContent(const vector<string>& content)
    { 
        if (content.size() > MAX_PAGE_LINES
            || canConsoleWriteToPage.load())
        {
            return;
        }

        lock_guard<mutex> lock(externalMutex);

        pageContent = {};
        pageHead = 0;
        pageCount = scast<u32>(content.size());

        for (u32 i = 0; i < pageCount; ++i)
        {
            pageContent[i] = content[i];
        }
    }

    void TUI::SendCommand(string_view command)
    {

    }

    void TUI::UpdateDisplayedContent()
    {
        if (!startedUpdate)
        {
            setlocale(LC_ALL, "");

#if defined(KLIN_ANY)
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

        if (!isEnabled) return;

        auto get_console_size = []() -> vec2
            {
#if defined(KWIN_ANY)
                CONSOLE_SCREEN_BUFFER_INFO csbi{};
                if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
                {
                    return vec2
                    {
                        scast<f32>(csbi.srWindow.Right - csbi.srWindow.Left + 1),
                        scast<f32>(csbi.srWindow.Bottom - csbi.srWindow.Top + 1)
                    };
                }
                else return vec2{ 80.0f, 25.0f };
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
#if defined(KWIN_ANY)
            _dup2(real_out, _fileno(stdout));
            _dup2(real_err, _fileno(stderr));
            SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(real_out));
            SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(real_err));
#else
            dup2(real_out, STDOUT_FILENO);
            dup2(real_err, STDERR_FILENO);
#endif

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
#if defined(KWIN_ANY)
            _dup2(cap_pipe[1], _fileno(stdout));
            _dup2(cap_pipe[1], _fileno(stderr));
            SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(cap_pipe[1]));
            SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(cap_pipe[1]));
#else
            dup2(cap_pipe[1], STDOUT_FILENO);
            dup2(cap_pipe[1], STDERR_FILENO);
#endif

            return;
        }

        static string nextFrameText{};
        if (!nextFrameText.empty())
        {
            cout << nextFrameText << "\n";
            cout.flush();
            sleep_for(milliseconds(5)); //wait for thread to push to pageContent
            nextFrameText.clear();
        }

        fflush(stdout);
        fflush(stderr);
        cout.flush();

#if defined(KWIN_ANY)
        _dup2(real_out, _fileno(stdout));
        _dup2(real_err, _fileno(stderr));
        SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(real_out));
        SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(real_err));
#else
        dup2(real_out, STDOUT_FILENO);
        dup2(real_err, STDERR_FILENO);
#endif

        auto draw_page_box = [&]() -> u32
            {
                u32 w = scast<u32>(totalSize.x);
                u32 h = scast<u32>(totalSize.y);

                u32 pageH = h - 3; //dont draw in bottom three rows
                u32 innerW = w - 2;
                u32 innerH = pageH - 2;

                string horizontalBar{};
                horizontalBar.reserve(innerW * 3);
                for (u32 i = 0; i < innerW; ++i) horizontalBar += "─";

                string spaces(innerW, ' ');

                //clear
                cout << "\x1b[2J\x1b[H";
                //disable auto-wrap
                cout << "\x1b[?7l";

                auto draw_top_border = [&]() -> void
                    {
                        if (pageTitle.empty()        //no content to draw
                            || pageTitle.size() < 3) //content is too short
                        {
                            cout << "┌" << horizontalBar << "┐";

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

                        cout << "┌" << lbar << display << rbar << "┐";
                    };

                draw_top_border();

                auto draw_middle = [&]() -> void
                    {
                        vector<string> wrapped{};
                        wrapped.reserve(innerH);

                        {
                            lock_guard<mutex> lock(externalMutex);
                            for (u32 i = 0; i < pageCount && wrapped.size() < innerH; ++i)
                            {
                                const string& src = pageContent[(pageHead + i) % MAX_PAGE_LINES];

                                if (src.empty())
                                {
                                    wrapped.emplace_back("");
                                    continue;
                                }

                                size_t start{};
                                while (start < src.size()
                                    && wrapped.size() < innerH)
                                {
                                    size_t remaining = src.size() - start;
                                    if (remaining <= innerW)
                                    {
                                        wrapped.push_back(src.substr(start));
                                        break;
                                    }

                                    //try to wrap on last space within innerW
                                    size_t searchLimit = start + innerW;
                                    size_t spacePos = src.rfind(' ', searchLimit);

                                    //rfind may find space before start - invalid
                                    if (spacePos != string::npos
                                        && spacePos > start
                                        && spacePos < searchLimit)
                                    {
                                        wrapped.push_back(src.substr(start, spacePos - start));
                                        start = spacePos + 1;
                                        while (start < src.size()
                                            && src[start] == ' ')
                                        {
                                            //skip extra spaces
                                            ++start;
                                        }
                                    }
                                    else
                                    {
                                        //long word - hard chop, show ... to indicate more
                                        string chunk = src.substr(start, innerW);
                                        bool hasMore = (start + innerW) < src.size();
                                        if (hasMore
                                            && innerW >= 3)
                                        {
                                            chunk = chunk.substr(0, innerW - 3) + "...";
                                            wrapped.push_back(chunk);
                                            //word ends here, don't continue to next row
                                            break;
                                        }
                                        else
                                        {
                                            wrapped.push_back(chunk);
                                            start += innerW;
                                        }
                                    }
                                }
                            }
                        }

                        for (u32 i = 0; i < innerH; ++i)
                        {
                            string line{};
                            if (i < wrapped.size())
                            {
                                line = wrapped[i];

                                //if line is shorter than innerW pad it, if it's exactly innerW keep it
                                if (line.size() < innerW) line += string(innerW - line.size(), ' ');
                                else if (line.size() > innerW)
                                {
                                    //safely chop with ...
                                    if (innerW >= 3) line = line.substr(0, innerW - 3) + "...";
                                    else line = line.substr(0, innerW);
                                }
                            }
                            else line = spaces;

                            cout << "\n│" << line << "│";
                        }
                    };
                    
                draw_middle();

                //bottom border
                cout << "\n└" << horizontalBar << "┘";

                cout.flush();

                return pageH + 1;
            };

        u32 inputStartRow = draw_page_box();

        auto draw_input_box = [&]() -> void
            {
                u32 w = scast<u32>(totalSize.x);
                u32 innerW = w - 2;

                string horizontalBar{};
                horizontalBar.reserve(innerW * 3);

                for (u32 i = 0; i < innerW; ++i) horizontalBar += "─";

                u32 pad = innerW > typedText.size()
                    ? innerW - typedText.size()
                    : 0;

                //snap cursor to where page box stopped + wrap still disabled
                cout << "\x1b[" << inputStartRow << ";1H";

                //top border
                cout << "┌" << horizontalBar << "┐\n";
                //middle
                cout << "│" << typedText << string(pad, ' ') << "│\n";
                //bottom border
                cout << "└" << horizontalBar << "┘";
                
                //snap cursor to input pos start
                cout << "\x1b[" << (inputStartRow + 1) << ";" << 2 << "H";

                //paste whole input text string + clear remainder
                cout << typedText << string(pad, ' ');

                //move cursor to cursorPos
                cout << "\x1b[" << (inputStartRow + 1) << ";" << (2 + cursorPos) << "H";

                //re-enable auto-wrap
                cout << "\x1b[?7h";

                cout.flush();
            };

        auto handle_input_char = [&](u32 c) -> void
            {
                u32 innerW = scast<u32>(totalSize.x) - 2;

                //printable character
                if (c >= 32
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

                ALLOWED_KEY key = scast<ALLOWED_KEY>(c);

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

                    if (typedHistoryPos == -1) typedHistoryPos = (int)typedTextCount - 1;
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

                    if (typedHistoryPos == -1) typedHistoryPos = 0;
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

#if defined(KWIN_ANY)
        _dup2(cap_pipe[1], _fileno(stdout));
        _dup2(cap_pipe[1], _fileno(stderr));
        SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(cap_pipe[1]));
        SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(cap_pipe[1]));
#else
        dup2(cap_pipe[1], STDOUT_FILENO);
        dup2(cap_pipe[1], STDERR_FILENO);
#endif
    }
}