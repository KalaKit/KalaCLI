//Copyright(C) 2026 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include "log_utils.hpp"
#include "file_utils.hpp"

#include "kc_tui.hpp"

using KalaHeaders::KalaLog::Log;
using KalaHeaders::KalaLog::LogType;

using KalaHeaders::KalaFile::ReadLinesFromFile;

using KalaCLI::TUI;

using std::string;
using std::vector;

static void PrefixlessAction(string& message)
{
    TUI::AppendToPage("user message: " + message);
}

int main()
{
    TUI::SetPrefixlessTargetAction([](string& msg)
        { 
            PrefixlessAction(msg);
        });

    TUI::AddCommand(
        {
            .primaryParam = "test",
            .description = "this is a test",
            .targetFunction = [](const vector<string>& params)
                {
                    Log::Print(
                        "testing",
                        "testing type",
                        LogType::LOG_INFO,
                        0,
                        true);
                }
        });

    TUI::AddCommand(
        {
            .primaryParam = "read",
            .description = "read lines from a text file",
            .targetFunction = [](const vector<string>& params)
                {
                    if (params.empty())
                    {
                        TUI::AppendToPage("ERROR: Failed to open file for reading because no path was given!");

                        return;
                    }
                    if (params.size() > 1)
                    {
                        TUI::AppendToPage("ERROR: Failed to open file for reading because too many params were given!");

                        return;
                    }

                    vector<string> outLines{};
                    string err = ReadLinesFromFile(params[0], outLines);
                    if (!err.empty())
                    {
                        TUI::AppendToPage("ERROR: Failed to open file for reading! Reason: " + err);
                        return;
                    }

                    TUI::SetPageContent(outLines);
                    TUI::SetPageTitle(params[0]);
                }
        });

    TUI::SetPageTitle("extra long title for no particular reason");
    TUI::Run();

    return 0;
}