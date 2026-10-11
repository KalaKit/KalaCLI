//Copyright(C) 2026 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include <chrono>
#include <thread>

#include "file_utils.hpp"

#include "kc_core.hpp"

using KalaHeaders::KalaFile::ReadLinesFromFile;

using KalaCLI::KalaCLICore;
using KalaCLI::COMMAND_PREFIX;

using std::string;
using std::vector;
using std::chrono::milliseconds;
using std::this_thread::sleep_for;

int main(int argc, char* argv[])
{
    //print user message
    KalaCLICore::SetPrefixlessInputAction([](string& msg)
        {
            KalaCLICore::AppendToPage("user message: " + msg);
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

    if (argc > 1)
    {
        string command{};
        for (int i = 1; i < argc; ++i)
        {
            if (i > 1) command += " ";
            command += argv[i];
        }

        KalaCLICore::SendCommand(command);

        //always exits if a command was passed, otherwise goes into cli mode
        exit(0);
    }

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

    KalaCLICore::SetConsoleWritesToPageState(true);
    KalaCLICore::SetWrapState(true);
    KalaCLICore::SetPageTitle("extra long title for no particular reason");

    KalaCLICore::AppendToPage(" ");
    KalaCLICore::AppendToPage("Type '" + string(COMMAND_PREFIX) + "help' to list all commands.");
    
    while(true)
    {
        KalaCLICore::UpdateDisplayedContent();

        sleep_for(milliseconds(33)); //30 frames per second
    }

    return 0;
}