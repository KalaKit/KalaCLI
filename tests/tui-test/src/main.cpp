//Copyright(C) 2026 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include "kc_tui.hpp"

using KalaCLI::TUI;

int main()
{
    TUI::SetPageTitle("extra long title for no particular reason");
    TUI::Run();

    return 0;
}