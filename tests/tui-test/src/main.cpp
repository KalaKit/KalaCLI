//Copyright(C) 2026 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include <string>
#include <array>
#include <thread>
#include <chrono>

#include "log_utils.hpp"

#include "kc_tui.hpp"

using KalaCLI::TUI;

using std::chrono::milliseconds;
using std::this_thread::sleep_for;

int main()
{
    TUI::SetEnabledState(true);
    TUI::SetConsoleWritesToPageState(true);

    TUI::SetPageTitle("extra long title bar text for no particular reason");

    while (true)
    {
        TUI::UpdateDisplayedContent();

        sleep_for(milliseconds(16));
    }

    return 0;
}