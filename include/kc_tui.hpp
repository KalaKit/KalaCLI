//Copyright(C) 2026 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#pragma once

#include <string>
#include <vector>

#include "core_utils.hpp"

namespace KalaCLI
{
    using std::string;
    using std::string_view;
    using std::vector;

    //how many newest lines to keep if console or external application keeps adding lines
    static constexpr u32 MAX_PAGE_LINES = 1000;
    //how far back to store typed text history
    static constexpr u32 MAX_TYPED_TEXT_HISTORY = 100;

    class LIB_API TUI
    {
    public:
        static bool IsEnabled();
        //Should the TUI be displayed or not, disabled by default,
        //enabling it prints custom borders and takes control over what the console can print,
        //may draw broken content if paired with other TUI systems or applications that draw complex content
        static void SetEnabledState(bool state);

        static bool CanConsoleWriteToPage();
        //Should new log messages be appended at the bottom of existing page content,
        //if disabled then console only allows to draw what the current page contains
        static void SetConsoleWritesToPageState(bool state);

        //Choose what to display at the top bar above the table
        static void SetPageTitle(string_view title);
        //Decide what to display in the active page
        static void SetPageContent(const vector<string>& content);
        //Alternative to manually typing a command
        static void SendCommand(string_view command);

        //Updates borders and content to fit new size
        //if old size does not match new size, call once per frame 
        static void UpdateDisplayedContent();
    };
}