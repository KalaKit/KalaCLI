//Copyright(C) 2026 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include <iostream>
#include <sstream>
#include <vector>
#include <filesystem>

#include "log_utils.hpp"
#include "string_utils.hpp"
#include "file_utils.hpp"

#include "kc_cli.hpp"
#include "kc_tui.hpp"
#include "kc_core.hpp"

using KalaHeaders::KalaLog::Log;
using KalaHeaders::KalaLog::LogType;

using KalaHeaders::KalaString::RemoveFromString;
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
using KalaCLI::CLI;

using std::cin;
using std::getline;
using std::system;
using std::ostringstream;
using std::string;
using std::vector;
using std::filesystem::current_path;
using std::filesystem::path;
using std::filesystem::weakly_canonical;
using std::filesystem::filesystem_error;

static void AddBuiltInCommands();

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
	static vector<Command> commands{};

	void CLI::Run(
		int argc,
		char* argv[],
		function<void()> AddExternalCommands)
	{
        if (CLI_COMMAND_PREFIX == TUI_COMMAND_PREFIX)
        {
            KalaCLICore::ForceClose(
                "KalaCLI CLI error",
                "Failed to run because cli and tui command prefixes are indentical!");
        }

		AddBuiltInCommands();
		if (AddExternalCommands) AddExternalCommands();

		//run the passed command if one was passed
		if (argc > 1)
		{
			vector<string> params{};
			for (int i = 1; i < argc; ++i) params.emplace_back(argv[i]);

			if (!params.empty())
			{
				if (!CLI::ParseCommand(params))
				{
					KalaCLICore::ForceClose(
						"KalaCLI CLI error",
						"Failed to parse params '" + string(*argv) + "'!");
				}
			}

			//always exits if a command was passed, otherwise goes into cli mode
			Command_Exit({"q"});
		}
		else Log::Print("Type '" + string(CLI_COMMAND_PREFIX) + "help' to list all commands.", true);

		string line{};
		while (true)
		{
			Log::Print("\nEnter command:", true);

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

				if (!ParseCommand(splitValue))
				{
					KalaCLICore::ForceClose(
						"KalaCLI CLI error",
						"Failed to parse '" + cleanedLine + "'!");
				}
			}
		}
	}

	vector<Command>& CLI::GetCommands() { return commands; }

	bool CLI::ParseCommand(const vector<string>& params)
	{
		if (params.empty()) return false;

		if (!CLI_COMMAND_PREFIX.empty()
			&& params[0].find(CLI_COMMAND_PREFIX.data()) == string::npos)
		{
			Log::Print(
				"Target command '" + params[0] + "' is missing required prefix '" + CLI_COMMAND_PREFIX.data() + "'!",
				"PARSE",
				LogType::LOG_ERROR,
				2);

				return false;
		}

		vector<string> cleanedParams = params;

		if (!CLI_COMMAND_PREFIX.empty())
		{
			string _ = RemoveFromString(
				cleanedParams[0],
				CLI_COMMAND_PREFIX.data(),
				cleanedParams[0]);
		}
		
		if (cleanedParams[0] == "run")
		{
			if (cleanedParams.size() == 1)
			{
				Log::Print(
					"Failed to run command '" + cleanedParams[0] + "'! You must pass 1 or more argument after the run command.",
					"PARSE",
					LogType::LOG_ERROR,
					2);
					
				return false;
			}
			
			auto Join = [](const vector<string>& params) -> string
				{
					ostringstream oss{};
					
					for (size_t i = 1; i < params.size(); ++i)
					{
						oss << params[i];
						if (i + 1 < params.size()) oss << ' ';
					}
					
					return oss.str();
				};
			
			system(Join(cleanedParams).c_str());
			
			return true;	
		}
		
		Command foundCommand{};

		for (const auto& c : commands)
		{
			if (c.primaryParam == cleanedParams[0])
			{
				foundCommand = c;
				break;
			}
		}

		if (foundCommand.primaryParam.empty())
		{
			Log::Print(
				"Failed to run command '" + cleanedParams[0] + "' because it does not exist! Type '" + string(CLI_COMMAND_PREFIX) + "help' to list all commands.",
				"PARSE",
				LogType::LOG_ERROR,
				2);
				
			return false;
		}

		foundCommand.targetFunction(cleanedParams);

		return true;
	}

	bool CLI::AddCommand(Command newValue)
	{
		if (newValue.primaryParam.empty())
		{
			Log::Print(
				"Skipped adding invalid command because it has no primary parameter!",
				"COMMAND",
				LogType::LOG_WARNING,
				true);

			return false;
		}
		if (!newValue.targetFunction)
		{
			Log::Print(
				"Skipped adding invalid command because it has no target function!",
				"COMMAND",
				LogType::LOG_WARNING,
				true);

			return false;
		}

		if (newValue.primaryParam.size() > 20)
		{
			Log::Print(
				"Skipped adding command with parameter '" + newValue.primaryParam + "' because it is too long.",
				"COMMAND",
				LogType::LOG_WARNING,
				true);

			return false;
		}

		//skip existing primary parameters
		for (const auto& c : commands)
		{
			if (newValue.primaryParam == c.primaryParam)
			{
				Log::Print(
					"Skipped adding command with primary parameter '" + newValue.primaryParam + "' because it has already been used in another command.",
					"COMMAND",
					LogType::LOG_WARNING,
					true);

				return false;
			}
		}

		commands.push_back(newValue);

		return true;
	}

	void CLI::Command_Help(const vector<string>& params)
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

		result << "\nType '" + string(CLI_COMMAND_PREFIX) + "info' with a command name as"
			<< " its argument to get more info about that command.\n"
			<< "Use the ampersand (&) symbol to stack commands, for example '" 
			+ string(CLI_COMMAND_PREFIX) + "list & " + string(CLI_COMMAND_PREFIX) + "q' to list and quick exit.\n\n"
			<< "Listing all commands:\n"
			<< "  run\n";
		for (const auto& c : CLI::GetCommands())
		{
			result << "  " << c.primaryParam << "\n";
		}

		Log::Print(result.str(), true);
	}

	void CLI::Command_Info(const vector<string>& params)
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
			Log::Print("Runs selected user command with any amount of parameters.", true);
			
			return;
		}

		Command cmd{};

		for (const auto& c : CLI::GetCommands())
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

		Log::Print(cmd.description, true);
	}

	void CLI::Command_Where(const vector<string>& params)
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
		Log::Print("\nCurrently at: " + currentDir, true);
	}

	void CLI::Command_List(const vector<string>& params)
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
			: (path(KalaCLICore::GetCurrentDir()) / targetDir).string() ;

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

		Log::Print(oss.str(), true);
	}

	void CLI::Command_Go(const vector<string>& params)
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

		Log::Print("\nMoved to new path: " + currentDir, true);
	}

	void CLI::Command_CreateDir(const vector<string>& params)
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

	void CLI::Command_Delete(const vector<string>& params)
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

	void CLI::Command_Rename(const vector<string>& params)
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

	void CLI::Command_Move(const vector<string>& params)
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

	void CLI::Command_Copy(const vector<string>& params)
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

	void CLI::Command_ForceCopy(const vector<string>& params)
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

	void CLI::Command_Clear(const vector<string>& params)
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

	void CLI::Command_Exit(const vector<string>& params)
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
			Log::Print(out.str(), true);

			Log::Print("Press 'Enter' to exit...", true);
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
}

void AddBuiltInCommands()
{
	Command cmd_help
	{
		.primaryParam = "help",
		.description = "Lists all available commands.",
		.targetFunction = KalaCLI::CLI::Command_Help
	};
	Command cmd_info
	{
		.primaryParam = "info",
		.description =
			"Lists info about chosen command, "
			"requires a target command as its argument.",
		.targetFunction = KalaCLI::CLI::Command_Info
	};

	Command cmd_where
	{
		.primaryParam = "where",
		.description =
			"Displays current path, "
			"requires a target path as its argument.",
		.targetFunction = KalaCLI::CLI::Command_Where
	};
	Command cmd_list
	{
		.primaryParam = "list",
		.description = "Lists all files and folders in current or target directory.",
		.targetFunction = KalaCLI::CLI::Command_List
	};
	Command cmd_go
	{
		.primaryParam = "go",
		.description =
			"Goes to chosen directory, "
			"requires a target path as its argument.",
		.targetFunction = KalaCLI::CLI::Command_Go
	};

	Command cmd_createdir
	{
		.primaryParam = "cd",
		.description =
			"Creates a new directory at the chosen path, "
			"requires a target path as its argument.",
		.targetFunction = KalaCLI::CLI::Command_CreateDir
	};
	Command cmd_delete
	{
		.primaryParam = "dl",
		.description =
			"Deletes file or directory at the chosen path, "
			"requires a target path as its argument.",
		.targetFunction = KalaCLI::CLI::Command_Delete
	};
	Command cmd_rename
	{
		.primaryParam = "rn",
		.description = 
			"Renames target file or directory to new value. "
			"Second path must be path to existing file, "
			"third parameter must be its new name only.",
		.targetFunction = KalaCLI::CLI::Command_Rename
	};
	Command cmd_move
	{
		.primaryParam = "mv",
		.description = 
			"Moves target file or directory to new path, "
			"overwrites file or directory at target path if it already exists.",
		.targetFunction = KalaCLI::CLI::Command_Move
	};
	Command cmd_copy
	{
		.primaryParam = "cp",
		.description =
			"Copies target file or directory to new chosen path, "
			"skips copy if new path already exists.",
		.targetFunction = KalaCLI::CLI::Command_Copy
	};
	Command cmd_forcecopy
	{
		.primaryParam = "fc",
		.description = 
			"Copies target file or directory to new chosen path, "
			"overwrites file or directory at target path if it already exists.",
		.targetFunction = KalaCLI::CLI::Command_ForceCopy
	};

	Command cmd_clear
	{
		.primaryParam = "c",
		.description = "Clears the console from all messages.",
		.targetFunction = KalaCLI::CLI::Command_Clear
	};
	Command cmd_exit
	{
		.primaryParam = "e",
		.description = "Asks for user to press enter to close the cli, good for reading messages before quitting.",
		.targetFunction = KalaCLI::CLI::Command_Exit
	};
	Command cmd_qe
	{
		.primaryParam = "q",
		.description = "Quickly exits this cli without any 'Press Enter to quit' confirmation.",
		.targetFunction = KalaCLI::CLI::Command_Exit
	};

	bool _ = CLI::AddCommand(cmd_help);
	_ = CLI::AddCommand(cmd_info);

	_ = CLI::AddCommand(cmd_where);
	_ = CLI::AddCommand(cmd_list);
	_ = CLI::AddCommand(cmd_go);

	_ = CLI::AddCommand(cmd_createdir);
	_ = CLI::AddCommand(cmd_delete);
	_ = CLI::AddCommand(cmd_rename);
	_ = CLI::AddCommand(cmd_move);
	_ = CLI::AddCommand(cmd_copy);
	_ = CLI::AddCommand(cmd_forcecopy);

	_ = CLI::AddCommand(cmd_clear);
	_ = CLI::AddCommand(cmd_exit);
	_ = CLI::AddCommand(cmd_qe);
}