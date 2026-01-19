//Copyright(C) 2026 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <csignal>

#include "KalaHeaders/log_utils.hpp"
#include "KalaHeaders/string_utils.hpp"
#include "KalaHeaders/file_utils.hpp"

#include "core.hpp"
#include "command.hpp"

using KalaHeaders::KalaLog::Log;
using KalaHeaders::KalaLog::LogType;
using KalaHeaders::KalaLog::TimeFormat;
using KalaHeaders::KalaLog::DateFormat;
using KalaHeaders::KalaString::ContainsString;
using KalaHeaders::KalaString::SplitString;
using KalaHeaders::KalaString::TrimString;
using KalaHeaders::KalaString::TokenizeString;
using KalaHeaders::KalaFile::ListDirectoryContents;

using KalaCLI::Core;
using KalaCLI::Command;
using KalaCLI::CommandManager;

using std::cin;
using std::getline;
using std::ostringstream;
using std::string;
using std::to_string;
using std::vector;
using std::filesystem::current_path;
using std::filesystem::path;
using std::raise;

static void AddBuiltInCommands();

//Built-in command for listing all commands
static void Command_Help(const vector<string>& params);
//Built-in command for listing info about chosen command
static void Command_Info(const vector<string>& params);

//Built-in command for listing current path
static void Command_Where(const vector<string>& params);
//Built-in command for listing all files and folders in current dir
static void Command_List(const vector<string>& params);
//Built-in command for going to desired path
static void Command_Go(const vector<string>& params);

//Built-in command for cleaning console commands
static void Command_Clear(const vector<string>& params);
//Built-in command for closing the cli
static void Command_Exit(const vector<string>& params);

namespace KalaCLI
{
	static string currentDir{};

	string& Core::GetCurrentDir() { return currentDir; }

	void Core::Run(
		int argc,
		char* argv[],
		function<void()> AddExternalCommands)
	{
		AddBuiltInCommands();
		if (AddExternalCommands) AddExternalCommands();

		//run the passed command if one was passed
		if (argc > 1)
		{
			vector<string> params{};
			for (int i = 1; i < argc; ++i) params.emplace_back(argv[i]);

			if (!params.empty()) CommandManager::ParseCommand(params);

			//always exits if a command was passed, otherwise goes into cli mode
			Command_Exit({});
		}

		string line{};
		while (true)
		{
			Log::Print("\nEnter command:");

			getline(cin, line);

			//uncomment if you want each new command to clean the console
			//system("cls");

			if (line.empty()) continue;

			vector<string> splitCommands{};
			if (ContainsString(line, "&"))
			{
				splitCommands = SplitString(line, "&");
			}
			else splitCommands.push_back(line);
			
			for (const auto& c : splitCommands)
			{
				string cleanedLine = TrimString(c);
				
				vector<string> splitValue{};
				char token{};
				if (cleanedLine.find('"') != string::npos) token = '"';
				if (cleanedLine.find('\'') != string::npos) token = '\'';
				
				if (token != 0)
				{
					splitValue = TokenizeString(
						cleanedLine,
						token,
						" ");
				}
				else splitValue = SplitString(cleanedLine, " ");

				if (splitValue.size() == 0) continue;

				CommandManager::ParseCommand(splitValue);
			}
		}
	}

	void Core::ForceClose(
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
	}
}

void AddBuiltInCommands()
{
	Command cmd_help
	{
		.primary = { "help" },
		.description = "Lists all available commands.",
		.paramCount = 1,
		.targetFunction = Command_Help
	};
	Command cmd_info
	{
		.primary = { "info" },
		.description = "Lists info about chosen command.",
		.paramCount = 2,
		.targetFunction = Command_Info
	};

	Command cmd_where
	{
		.primary = { "where" },
		.description = "Displays current path.",
		.paramCount = 1,
		.targetFunction = Command_Where
	};
	Command cmd_list
	{
		.primary = { "list" },
		.description = "Lists all files and folders in current directory.",
		.paramCount = 1,
		.targetFunction = Command_List
	};
	Command cmd_go
	{
		.primary = { "go" },
		.description = "Goes to chosen directory.",
		.paramCount = 2,
		.targetFunction = Command_Go
	};

	Command cmd_clear
	{
		.primary = { "clear", "c" },
		.description = "Clears the console from all messages.",
		.paramCount = 1,
		.targetFunction = Command_Clear
	};
	Command cmd_exit
	{
		.primary = { "exit", "e" },
		.description = "Asks for user to press enter to close the cli, good for reading messages before quitting.",
		.paramCount = 1,
		.targetFunction = Command_Exit
	};
	Command cmd_qe
	{
		.primary = { "quickexit", "qe" },
		.description = "Quickly exits this cli without any 'Press Enter to quit' confirmation.",
		.paramCount = 1,
		.targetFunction = Command_Exit
	};

	CommandManager::AddCommand(cmd_help);
	CommandManager::AddCommand(cmd_info);

	CommandManager::AddCommand(cmd_where);
	CommandManager::AddCommand(cmd_list);
	CommandManager::AddCommand(cmd_go);

	CommandManager::AddCommand(cmd_clear);
	CommandManager::AddCommand(cmd_exit);
	CommandManager::AddCommand(cmd_qe);
}

void Command_Help(const vector<string>& params)
{
	ostringstream result{};

	result << "\nType 'info' with a command name as the"
		<< " second parameter to get more info about that command.\n"
		<< "Use the ampersand (&) symbol to stack commands, for example '--list & --qe' to list and quick exit.\n\n"
		<< "Listing all commands:\n"
		<< "  run, r\n";
	for (const auto& c : CommandManager::GetCommands())
	{
		for (const auto& p : c.primary)
		{
			if (p == c.primary[0]) result << "  ";
			result << p;
			if (p != c.primary[c.primary.size() - 1])
			{
				result << ", ";
			}
			else result << "\n";
		}
	}

	Log::Print(result.str());
}

void Command_Info(const vector<string>& params)
{
	string command = params[1];

	ostringstream result{};

	result << "\n";
	
	if (command == "run"
		|| command == "r")
	{
		result << "Runs selected user command with any amount of parameters.";
		
		Log::Print(result.str());
		
		return;
	}

	Command cmd{};

	for (const auto& c : CommandManager::GetCommands())
	{
		if (find(c.primary.begin(), c.primary.end(), command) != c.primary.end())
		{
			cmd = c;
			break;
		}
	}
	
	if (cmd.primary.empty()
		&& cmd.paramCount == 0
		&& !cmd.targetFunction)
	{
		Log::Print(
			"Cannot print info about a command that doesn't exist!",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}

	result << "primary variants: ";
	for (const auto& p : cmd.primary)
	{
		result << p;
		if (p != cmd.primary[cmd.primary.size() - 1])
		{
			result << ", ";
		}
		else result << "\n";
	}

	result << "description: " << cmd.description << "\n";
	result << "parameter count: " << to_string(cmd.paramCount);

	Log::Print(result.str());
}

void Command_Where(const vector<string>& params)
{
	string& currentDir = Core::GetCurrentDir();

	if (currentDir.empty()) currentDir = current_path().string();
	Log::Print("\nCurrently at: " + currentDir);
}

void Command_List(const vector<string>& params)
{
	string& currentDir = Core::GetCurrentDir();

	if (currentDir.empty()) currentDir = current_path().string();

	vector<path> content{};

	string result = ListDirectoryContents(currentDir, content);

	if (!result.empty())
	{
		Log::Print(
			"Failed to list current directory contents! Reason: " + result,
			"COMMAND",
			LogType::LOG_ERROR,
			2);

		return;
	}

	ostringstream oss{};

	oss << "\nListing all paths at '" << currentDir << "':\n";
	if (content.empty()) oss << "  - (empty)";
	else
	{
		for (size_t i = 0; i < content.size(); ++i)
		{
			oss << "  - ";

			path rel = content[i].lexically_relative(currentDir);
			oss << rel.string();

			if (is_directory(content[i])) oss << "/";

			if (i + 1 < content.size()) oss << "\n";
		}
	}

	Log::Print(oss.str());
}

void Command_Go(const vector<string>& params)
{
	string& currentDir = Core::GetCurrentDir();

	if (currentDir.empty()) currentDir = current_path().string();
	path correctTarget = weakly_canonical(path(currentDir) / params[1]);

	if (!exists(correctTarget))
	{
		ostringstream oss{};
		oss << "Cannot go to target path '" << correctTarget
			<< "' because it does not exist!";

		Log::Print(
			oss.str(),
			"COMMAND",
			LogType::LOG_ERROR,
			2);

		return;
	}

	if (!is_directory(correctTarget))
	{
		ostringstream oss{};
		oss << "Cannot go to target path '" << correctTarget
			<< "' because it is not a directory!";

		Log::Print(
			oss.str(),
			"COMMAND",
			LogType::LOG_ERROR,
			2);

		return;
	}

	currentDir = correctTarget.string();

	Log::Print("\nMoved to new path: " + currentDir);
}

void Command_Clear(const vector<string>& params) { system("cls"); }

void Command_Exit(const vector<string>& params)
{
	if (params.size() == 1
		&& (params[0] == "exit"
			|| params[0] == "e"))
	{
		ostringstream out{};
		out << "\n==========================================================================================\n";
		Log::Print(out.str());

		Log::Print("Press 'Enter' to exit...");
		cin.get();
	}

	quick_exit(0);
}