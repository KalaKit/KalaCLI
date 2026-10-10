//Copyright(C) 2026 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include <chrono>
#include <thread>

#include "log_utils.hpp"
#include "file_utils.hpp"

#include "kc_core.hpp"

using KalaHeaders::KalaLog::Log;
using KalaHeaders::KalaLog::LogType;

using KalaHeaders::KalaFile::ReadLinesFromFile;

using KalaCLI::KalaCLICore;
using KalaCLI::PageDirection;
using KalaCLI::COMMAND_PREFIX;

using std::string;
using std::vector;
using std::chrono::milliseconds;
using std::this_thread::sleep_for;

static void PrefixlessAction(string& message)
{
    KalaCLICore::AppendToPage("user message: " + message);
}

int main(int argc, char* argv[])
{
    if (argc > 1)
    {
        string command{};
        for (int i = 1; i < argc; ++i)
        {
            if (i > 1) command += " ";
            command += argv[i];
        }

        if (!command.empty())
        {
            KalaCLICore::SendCommand(command);
        }

        //always exits if a command was passed, otherwise goes into cli mode
        exit(0);
    }
    else Log::Print("Type '" + string(COMMAND_PREFIX) + "help' to list all commands.", true);

    //test complex text file reading
    KalaCLICore::AddCommand(
        {
            .primaryParam = "read",
            .description = "read lines from a text file",
            .targetFunction = [](const vector<string>& params)
                {
                    if (params.empty())
                    {
                        KalaCLICore::AppendToPage("ERROR: Failed to open file for reading because no path was given!");

                        return;
                    }
                    if (params.size() > 1)
                    {
                        KalaCLICore::AppendToPage("ERROR: Failed to open file for reading because too many params were given!");

                        return;
                    }

                    vector<string> outLines{};
                    string err = ReadLinesFromFile(params[0], outLines);
                    if (!err.empty())
                    {
                        KalaCLICore::AppendToPage("ERROR: Failed to open file for reading! Reason: " + err);
                        return;
                    }

                    KalaCLICore::SetPageContent(outLines);
                    KalaCLICore::SetPageTitle(params[0]);
                }
        });

    //test console wrapping states
    KalaCLICore::AddCommand(
        {
            .primaryParam = "tw",
            .description = "toggle wrapping",
            .targetFunction = [](const vector<string>& _)
                {
                    KalaCLICore::SetWrapState(!KalaCLICore::GetWrapState());
                }
        });

    //first time initialization prepares bounds for necessary checks...
    KalaCLICore::UpdateDisplayedContent();

    //add written prefixless message as user message
    KalaCLICore::SetPrefixlessInputAction([](string& msg)
        { 
            PrefixlessAction(msg);
        });

    //draw an extra long title to test title bar horizontal auto-scrolling if console is thin
    KalaCLICore::SetPageTitle("extra long title for no particular reason");

    /*
    //draw a permanent row at the top, never overwritten by other content
    KalaCLICore::OverwriteRow(
        "##### overwritten top row #####",
        PageDirection::D_DOWN,
        0);

    //draw a permanent row at the bottom, never overwritten by other content
    KalaCLICore::OverwriteRow(
        "##### overwritten bottom row (second) #####",
        PageDirection::D_UP,
        1);

    //draw a permanent row at the bottom, never overwritten by other content
    KalaCLICore::OverwriteRow(
        "##### overwritten bottom row (first) #####",
        PageDirection::D_UP,
        0);

    //lock the top row and two bottom rows so they are not accounted as scrollable and highlightable areas
    KalaCLICore::SetPageScrollRange({ 1, 2 });
    */
    
    while(true)
    {
        KalaCLICore::UpdateDisplayedContent();

        sleep_for(milliseconds(33)); //30 frames per second
    }

    return 0;
}