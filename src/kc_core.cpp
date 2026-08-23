//Copyright(C) 2026 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#ifdef __linux__
#include <csignal>
#endif

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#include "log_utils.hpp"
#include "string_utils.hpp"
#include "file_utils.hpp"

#include "kc_core.hpp"
#include "kc_command.hpp"

using KalaHeaders::KalaLog::Log;
using KalaHeaders::KalaLog::LogType;
using KalaHeaders::KalaLog::TimeFormat;
using KalaHeaders::KalaLog::DateFormat;
using KalaHeaders::KalaString::SplitString;
using KalaHeaders::KalaString::TrimString;
using KalaHeaders::KalaString::TokenizeString;
using KalaHeaders::KalaFile::ListDirectoryContents;
using KalaHeaders::KalaFile::CreateNewDirectory;
using KalaHeaders::KalaFile::DeletePath;
using KalaHeaders::KalaFile::RenamePath;
using KalaHeaders::KalaFile::MovePath;
using KalaHeaders::KalaFile::CopyPath;

using KalaCLI::KalaCLICore;
using KalaCLI::Command;
using KalaCLI::CommandManager;
using KalaCLI::COMMAND_PREFIX;

#ifdef __linux__
using std::raise;
#endif

using std::cin;
using std::getline;
using std::ostringstream;
using std::string;
using std::vector;
using std::filesystem::current_path;
using std::filesystem::path;
using std::filesystem::weakly_canonical;
using std::filesystem::filesystem_error;

static void AddBuiltInCommands();

//Built-in command for listing all commands
static void Command_Help(const vector<string>& params);
//Built-in command for listing info about chosen command
static void Command_Info(const vector<string>& params);

//Built-in command for listing current path
static void Command_Where(const vector<string>& params);
//Built-in command for listing all files and folders in current dir
static void Command_List(const vector<string>& params);
//Built-in command for going to target path
static void Command_Go(const vector<string>& params);

//Built-in command for creating a directory at the target path
static void Command_CreateDir(const vector<string>& params);
//Built-in command for renaming the file or directory at the target path
static void Command_Rename(const vector<string>& params);
//Built-in command for deleting the file or directory at the target path
static void Command_Delete(const vector<string>& params);
//Built-in command for moving the file or directory from the origin to the target path,
//the file or directory at the target path is overridden if it already exists
static void Command_Move(const vector<string>& params);
//Built-in command for copying the file or directory from the origin to the target path,
//the copy action bails if a file or directory already exists at the target path
static void Command_Copy(const vector<string>& params);
//Built-in command for copying the file or directory from the origin to the target path,
//the file or directory at the target path is overridden if it already exists
static void Command_ForceCopy(const vector<string>& params);

//Built-in command for cleaning console commands
static void Command_Clear(const vector<string>& params);
//Built-in command for closing the cli
static void Command_Exit(const vector<string>& params);

static path TryTargetPath(
	const string& target, 
	const string& action,
	bool isFull = false)
{
	path invalidTarget{};

	string targetType = isFull ? "full" : "partial";
	path finalTarget = isFull
		? path(target)
		: path(KalaCLICore::GetCurrentDir()) / target;

	try
	{
		finalTarget = weakly_canonical(finalTarget);
	}
	catch (const filesystem_error&)
	{
		Log::Print(
			"Failed to " + action + " target via " + targetType + " path '" + finalTarget.string() + "' because it could not be resolved!",
			"COMMAND",
			LogType::LOG_ERROR,
			2);

		return invalidTarget;
	}

	return finalTarget;
}

namespace KalaCLI
{
	static string currentDir{};

	string& KalaCLICore::GetCurrentDir() { return currentDir; }

	void KalaCLICore::Run(
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

			if (!params.empty())
			{
				if (!CommandManager::ParseCommand(params))
				{
					KalaCLICore::ForceClose(
						"KalaCLI core error",
						"Failed to parse params '" + string(*argv) + "'!");
				}
			}

			//always exits if a command was passed, otherwise goes into cli mode
			Command_Exit({"q"});
		}
		else Log::Print("Type '" + string(COMMAND_PREFIX) + "help' to list all commands.");

		string line{};
		while (true)
		{
			Log::Print("\nEnter command:");

			getline(cin, line);

			if (line.empty()) continue;

			vector<string> splitCommands{};
			if (line.find("&") != string::npos)
			{
				vector<string> sl{};
				string _ = SplitString(line, "&", sl);
				splitCommands = sl;
			}
			else splitCommands.push_back(line);
			
			for (const auto& c : splitCommands)
			{
				string cleanedLine{};
				string _ = TrimString(c, cleanedLine);
				
				vector<string> splitValue{};
				char token{};
				if (cleanedLine.find('"') != string::npos) token = '"';
				if (cleanedLine.find('\'') != string::npos) token = '\'';
				
				if (token != 0)
				{
					vector<string> ts{};
					string _ = TokenizeString(
						cleanedLine,
						token,
						" ",
						ts);

					splitValue = ts;
				}
				else
				{
					vector<string> ss{};
					string _ = SplitString(cleanedLine, " ", ss);
					splitValue = ss;
				}

				if (splitValue.size() == 0) continue;

				if (!CommandManager::ParseCommand(splitValue))
				{
					KalaCLICore::ForceClose(
						"KalaCLI core error",
						"Failed to parse '" + cleanedLine + "'!");
				}
			}
		}
	}

	void KalaCLICore::ForceClose(
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

		abort();
	}
}

void AddBuiltInCommands()
{
	Command cmd_help
	{
		.primaryParam = "help",
		.description = "Lists all available commands.",
		.targetFunction = Command_Help
	};
	Command cmd_info
	{
		.primaryParam = "info",
		.description =
			"Lists info about chosen command, "
			"requires a target command as its argument.",
		.targetFunction = Command_Info
	};

	Command cmd_where
	{
		.primaryParam = "where",
		.description =
			"Displays current path, "
			"requires a target path as its argument.",
		.targetFunction = Command_Where
	};
	Command cmd_list
	{
		.primaryParam = "list",
		.description = "Lists all files and folders in current or target directory.",
		.targetFunction = Command_List
	};
	Command cmd_go
	{
		.primaryParam = "go",
		.description =
			"Goes to chosen directory, "
			"requires a target path as its argument.",
		.targetFunction = Command_Go
	};

	Command cmd_createdir
	{
		.primaryParam = "cd",
		.description =
			"Creates a new directory at the chosen path, "
			"requires a target path as its argument.",
		.targetFunction = Command_CreateDir
	};
	Command cmd_delete
	{
		.primaryParam = "dl",
		.description =
			"Deletes file or directory at the chosen path, "
			"requires a target path as its argument.",
		.targetFunction = Command_Delete
	};
	Command cmd_rename
	{
		.primaryParam = "rn",
		.description = 
			"Renames target file or directory to new value. "
			"Second path must be path to existing file, "
			"third parameter must be its new name only.",
		.targetFunction = Command_Rename
	};
	Command cmd_move
	{
		.primaryParam = "mv",
		.description = 
			"Moves target file or directory to new path, "
			"overwrites file or directory at target path if it already exists.",
		.targetFunction = Command_Move
	};
	Command cmd_copy
	{
		.primaryParam = "cp",
		.description =
			"Copies target file or directory to new chosen path, "
			"skips copy if new path already exists.",
		.targetFunction = Command_Copy
	};
	Command cmd_forcecopy
	{
		.primaryParam = "fc",
		.description = 
			"Copies target file or directory to new chosen path, "
			"overwrites file or directory at target path if it already exists.",
		.targetFunction = Command_ForceCopy
	};

	Command cmd_clear
	{
		.primaryParam = "c",
		.description = "Clears the console from all messages.",
		.targetFunction = Command_Clear
	};
	Command cmd_exit
	{
		.primaryParam = "e",
		.description = "Asks for user to press enter to close the cli, good for reading messages before quitting.",
		.targetFunction = Command_Exit
	};
	Command cmd_qe
	{
		.primaryParam = "q",
		.description = "Quickly exits this cli without any 'Press Enter to quit' confirmation.",
		.targetFunction = Command_Exit
	};

	bool _ = CommandManager::AddCommand(cmd_help);
	_ = CommandManager::AddCommand(cmd_info);

	_ = CommandManager::AddCommand(cmd_where);
	_ = CommandManager::AddCommand(cmd_list);
	_ = CommandManager::AddCommand(cmd_go);

	_ = CommandManager::AddCommand(cmd_createdir);
	_ = CommandManager::AddCommand(cmd_delete);
	_ = CommandManager::AddCommand(cmd_rename);
	_ = CommandManager::AddCommand(cmd_move);
	_ = CommandManager::AddCommand(cmd_copy);
	_ = CommandManager::AddCommand(cmd_forcecopy);

	_ = CommandManager::AddCommand(cmd_clear);
	_ = CommandManager::AddCommand(cmd_exit);
	_ = CommandManager::AddCommand(cmd_qe);
}

void Command_Help(const vector<string>& params)
{
	if (params.size() > 1)
	{
		Log::Print(
			"Command 'help' does not allow any arguments!",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}

	ostringstream result{};

	result << "\nType '" + string(COMMAND_PREFIX) + "info' with a command name as"
		<< " its argument to get more info about that command.\n"
		<< "Use the ampersand (&) symbol to stack commands, for example '" 
		+ string(COMMAND_PREFIX) + "list & " + string(COMMAND_PREFIX) + "q' to list and quick exit.\n\n"
		<< "Listing all commands:\n"
		<< "  run\n";
	for (const auto& c : CommandManager::GetCommands())
	{
		result << "  " << c.primaryParam << "\n";
	}

	Log::Print(result.str());
}

void Command_Info(const vector<string>& params)
{
	if (params.size() == 1)
	{
		Log::Print(
			"Command 'info' got no arguments! You must pass one target command name.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}
	if (params.size() > 2)
	{
		Log::Print(
			"Command 'info' only allows one argument! You must pass one target command name.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}

	string command = params[1];
	
	if (command == "run")
	{
		Log::Print("Runs selected user command with any amount of parameters.");
		
		return;
	}

	Command cmd{};

	for (const auto& c : CommandManager::GetCommands())
	{
		if (c.primaryParam == command)
		{
			cmd = c;
			break;
		}
	}
	
	if (cmd.primaryParam.empty())
	{
		Log::Print(
			"Failed to get info for command '" + params[1] + "' because it does not exist!",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}

	Log::Print(cmd.description);
}

void Command_Where(const vector<string>& params)
{
	if (params.size() > 1)
	{
		Log::Print(
			"Command 'where' does not allow any arguments!",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}

	string& currentDir = KalaCLICore::GetCurrentDir();

	if (currentDir.empty()) currentDir = current_path().string();
	Log::Print("\nCurrently at: " + currentDir);
}

void Command_List(const vector<string>& params)
{
	if (params.size() > 2)
	{
		Log::Print(
			"Command 'list' only allows one optional argument! You must pass one path or no arguments.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}

	string targetDir = params.size() == 1 
		? KalaCLICore::GetCurrentDir()
		: params[1];

	if (targetDir.empty()) targetDir = params.size() == 1 
		? current_path().string()
		: (path(KalaCLI::currentDir) / targetDir).string() ;

	vector<path> content{};

	string result = ListDirectoryContents(targetDir, content);

	if (!result.empty())
	{
		Log::Print(
			"Failed to list target directory contents! Reason: " + result,
			"COMMAND",
			LogType::LOG_ERROR,
			2);

		return;
	}

	ostringstream oss{};

	oss << "\nListing all paths at '" << targetDir << "':\n";
	if (content.empty()) oss << "  - (empty)";
	else
	{
		for (size_t i = 0; i < content.size(); ++i)
		{
			oss << "  - ";

			path rel = content[i].lexically_relative(targetDir);
			oss << rel.string();

			if (is_directory(content[i])) oss << "/";

			if (i + 1 < content.size()) oss << "\n";
		}
	}

	Log::Print(oss.str());
}

void Command_Go(const vector<string>& params)
{
	if (params.size() == 1)
	{
		Log::Print(
			"Command 'go' got no arguments! You must pass one path.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}
	if (params.size() > 2)
	{
		Log::Print(
			"Command 'go' only allows one argument! You must pass one path.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}

	string& currentDir = KalaCLICore::GetCurrentDir();

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

void Command_CreateDir(const vector<string>& params)
{
	if (params.size() == 1)
	{
		Log::Print(
			"Command 'cd' got no arguments! You must pass one path.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}
	if (params.size() > 2)
	{
		Log::Print(
			"Command 'cd' only allows one argument! You must pass one path.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}

	path partialPath = TryTargetPath(
		params[1],
		"create directory at");

	if (partialPath.empty()) return;

	auto createdir = [](path target)
		{
			string result = CreateNewDirectory(target);

			if (!result.empty())
			{
				Log::Print(
					result, 
					"COMMAND",
					LogType::LOG_ERROR,
					2);
			}
		};

	if (!exists(partialPath))
	{
		createdir(partialPath);

		return;
	}

	string& currentDir = KalaCLICore::GetCurrentDir();
	if (currentDir.empty()) currentDir = current_path().string();

	path fullPath = TryTargetPath(
		params[1],
		"create directory at",
		true);

	if (fullPath.empty()) return;

	if (!exists(fullPath))
	{
		createdir(fullPath);

		return;
	}

	Log::Print(
		"Cannot create a new directory at target path '" + fullPath.string() + "' because it already exists!",
		"COMMAND",
		LogType::LOG_ERROR,
		2);
}

void Command_Delete(const vector<string>& params)
{
	if (params.size() == 1)
	{
		Log::Print(
			"Command 'dl' got no arguments! You must pass one path.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}
	if (params.size() > 2)
	{
		Log::Print(
			"Command 'dl' only allows one argument! You must pass one path.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}

	path partialPath = TryTargetPath(
		params[1],
		"delete");

	if (partialPath.empty()) return;

	auto deletepath = [](path target)
		{
			string result = DeletePath(target);

			if (!result.empty())
			{
				Log::Print(
					result,
					"COMMAND",
					LogType::LOG_ERROR,
					2);
			}
		};

	if (exists(partialPath))
	{
		deletepath(partialPath);

		return;
	}

	string& currentDir = KalaCLICore::GetCurrentDir();
	if (currentDir.empty()) currentDir = current_path().string();

	path fullPath = TryTargetPath(
		params[1],
		"delete",
		true);

	if (fullPath.empty()) return;

	if (exists(fullPath))
	{
		deletepath(fullPath);

		return;
	}

	Log::Print(
		"Cannot delete target path '" + fullPath.string() + "' because it does not exist!",
		"COMMAND",
		LogType::LOG_ERROR,
		2);
}

void Command_Rename(const vector<string>& params)
{
	if (params.size() == 1)
	{
		Log::Print(
			"Command 'rn' got no arguments! You must pass origin path and target name.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}
	if (params.size() == 2)
	{
		Log::Print(
			"Command 'rn' requires two arguments! You must pass origin path and target name.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}
	if (params.size() > 3)
	{
		Log::Print(
			"Command 'rn' only allows two arguments! You must pass origin path and target name.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}

	path partialPath = TryTargetPath(
		params[1],
		"rename");

	if (partialPath.empty()) return;

	string newName = params[2];

	auto renamepath = [](path target, string newName)
		{
			string result = RenamePath(target, newName);

			if (!result.empty())
			{
				Log::Print(
					result,
					"COMMAND",
					LogType::LOG_ERROR,
					2);
			}
		};

	if (exists(partialPath))
	{
		renamepath(partialPath, newName);

		return;
	}

	string& currentDir = KalaCLICore::GetCurrentDir();
	if (currentDir.empty()) currentDir = current_path().string();
	
	path fullPath = TryTargetPath(
		params[1],
		"rename",
		true);

	if (fullPath.empty()) return;

	if (exists(fullPath))
	{
		renamepath(fullPath, newName);

		return;
	}

	Log::Print(
		"Cannot rename target path '" + fullPath.string() + "' because it does not exist!",
		"COMMAND",
		LogType::LOG_ERROR,
		2);
}

void Command_Move(const vector<string>& params)
{
	if (params.size() == 1)
	{
		Log::Print(
			"Command 'mv' got no arguments! You must pass origin path and target path.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}
	if (params.size() == 2)
	{
		Log::Print(
			"Command 'mv' requires two arguments! You must pass origin path and target path.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}
	if (params.size() > 3)
	{
		Log::Print(
			"Command 'mv' only allows two arguments! You must pass origin path and target path.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}

	string& currentDir = KalaCLICore::GetCurrentDir();
	if (currentDir.empty()) currentDir = current_path().string();

	path partialOrigin = TryTargetPath(
		params[1],
		"move");
	if (partialOrigin.empty()) return;

	path partialTarget = TryTargetPath(
		params[2],
		"move");
	if (partialTarget.empty()) return;
	
	path fullOrigin = TryTargetPath(
		params[1],
		"move",
		true);
	if (fullOrigin.empty()) return;

	path fullTarget = TryTargetPath(
		params[2],
		"move",
		true);
	if (fullTarget.empty()) return;

	auto movepath = [](path origin, path target)
		{
			string result = MovePath(origin, target);

			if (!result.empty())
			{
				Log::Print(
					result,
					"COMMAND",
					LogType::LOG_ERROR,
					2);
			}
		};

	path correctOrigin = exists(partialOrigin) ? partialOrigin : fullOrigin;
	if (!exists(correctOrigin))
	{
		Log::Print(
			"Cannot move origin path '" + correctOrigin.string() + "' because it does not exist!",
			"COMMAND",
			LogType::LOG_ERROR,
			2);

		return;
	}

	//use move target as full target if path contains full disk name like 'C:'
	path correctTarget = fullTarget.has_root_name() ? fullTarget : partialTarget;

	movepath(correctOrigin, correctTarget);
}

void Command_Copy(const vector<string>& params)
{
	if (params.size() == 1)
	{
		Log::Print(
			"Command 'cp' got no arguments! You must pass origin path and target path.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}
	if (params.size() == 2)
	{
		Log::Print(
			"Command 'cp' requires two arguments! You must pass origin path and target path.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}
	if (params.size() > 3)
	{
		Log::Print(
			"Command 'cp' only allows two arguments! You must pass origin path and target path.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}

	string& currentDir = KalaCLICore::GetCurrentDir();
	if (currentDir.empty()) currentDir = current_path().string();

	path partialOrigin = TryTargetPath(
		params[1],
		"copy");
	if (partialOrigin.empty()) return;

	path partialTarget = TryTargetPath(
		params[2],
		"copy");
	if (partialTarget.empty()) return;

	path fullOrigin = TryTargetPath(
		params[1],
		"copy",
		true);
	if (fullOrigin.empty()) return;

	path fullTarget = TryTargetPath(
		params[2],
		"copy",
		true);
	if (fullTarget.empty()) return;

	auto copypath = [](path origin, path target)
		{
			string result = CopyPath(origin, target);

			if (!result.empty())
			{
				Log::Print(
					result,
					"COMMAND",
					LogType::LOG_ERROR,
					2);
			}
		};

	path correctOrigin = exists(partialOrigin) ? partialOrigin : fullOrigin;
	if (!exists(correctOrigin))
	{
		Log::Print(
			"Cannot copy origin path '" + correctOrigin.string() + "' because it does not exist!",
			"COMMAND",
			LogType::LOG_ERROR,
			2);

		return;
	}

	//use copy target as full target if path contains full disk name like 'C:'
	path correctTarget = fullTarget.has_root_name() ? fullTarget : partialTarget;

	if (exists(correctTarget))
	{
		Log::Print(
			"Cannot copy origin path '" + fullOrigin.string() + "' to target path '" + correctTarget.string()  + "' because the target already exists!",
			"COMMAND",
			LogType::LOG_ERROR,
			2);

		return;
	}

	copypath(correctOrigin, correctTarget);
}

void Command_ForceCopy(const vector<string>& params)
{
	if (params.size() == 1)
	{
		Log::Print(
			"Command 'fc' got no arguments! You must pass origin path and target path.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}
	if (params.size() == 2)
	{
		Log::Print(
			"Command 'fc' requires two arguments! You must pass origin path and target path.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}
	if (params.size() > 3)
	{
		Log::Print(
			"Command 'fc' only allows two arguments! You must pass origin path and target path.",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}

	string& currentDir = KalaCLICore::GetCurrentDir();
	if (currentDir.empty()) currentDir = current_path().string();

	path partialOrigin = TryTargetPath(
		params[1],
		"force copy");
	if (partialOrigin.empty()) return;

	path partialTarget = TryTargetPath(
		params[2],
		"force copy");
	if (partialTarget.empty()) return;

	path fullOrigin = TryTargetPath(
		params[1],
		"force copy",
		true);
	if (fullOrigin.empty()) return;

	path fullTarget = TryTargetPath(
		params[2],
		"force copy",
		true);
	if (fullTarget.empty()) return;

	auto copypath = [](path origin, path target)
		{
			string result = CopyPath(origin, target, true);

			if (!result.empty())
			{
				Log::Print(
					result,
					"COMMAND",
					LogType::LOG_ERROR,
					2);
			}
		};

	path correctOrigin = exists(partialOrigin) ? partialOrigin : fullOrigin;
	if (!exists(correctOrigin))
	{
		Log::Print(
			"Cannot force copy origin path '" + correctOrigin.string() + "' because it does not exist!",
			"COMMAND",
			LogType::LOG_ERROR,
			2);

		return;
	}

	//use force copy target as full target if path contains full disk name like 'C:'
	path correctTarget = fullTarget.has_root_name() ? fullTarget : partialTarget;

	copypath(correctOrigin, correctTarget);
}

void Command_Clear(const vector<string>& params)
{
	if (params.size() > 1)
	{
		Log::Print(
			"Command 'c' does not allow any arguments!",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}

#ifdef _WIN32
	system("cls");
#else
	system("clear");
#endif
}

void Command_Exit(const vector<string>& params)
{
	if (params.empty()) quick_exit(0);

	if (params.size() > 1)
	{
		Log::Print(
			"Command 'e' and command 'q' does not allow any arguments!",
			"PARSE",
			LogType::LOG_ERROR,
			2);

		return;
	}

	if (params[0] == "q") quick_exit(0);
	else if (params[0] == "e")
	{
		ostringstream out{};
		out << "\n==========================================================================================\n";
		Log::Print(out.str());

		Log::Print("Press 'Enter' to exit...");
		cin.get();
	}
	else
	{
		Log::Print(
			"Unknown exit type '" + params[0] + "' was passed! Only 'q' and 'e' are allowed.",
			"PARSE",
			LogType::LOG_ERROR,
			2);
	}
}
