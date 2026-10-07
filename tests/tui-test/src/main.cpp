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

using std::string;
using std::vector;
using std::chrono::milliseconds;
using std::this_thread::sleep_for;

static void PrefixlessAction(string& message)
{
    KalaCLICore::AppendToPage("user message: " + message);
}

int main()
{
    /*
    KalaCLICore::SetPrefixlessInputAction([](string& msg)
        { 
            PrefixlessAction(msg);
        });
    */

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

    KalaCLICore::SetPageTitle("extra long title for no particular reason");
    
    while(true)
    {
        KalaCLICore::UpdateDisplayedContent();

        sleep_for(milliseconds(16));
    }

    return 0;
}