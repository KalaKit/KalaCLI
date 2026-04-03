//Copyright(C) 2026 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#include <sstream>

#include "log_utils.hpp"
#include "string_utils.hpp"

#include "kc_command.hpp"

using KalaHeaders::KalaLog::Log;
using KalaHeaders::KalaLog::LogType;
using KalaHeaders::KalaString::RemoveFromString;

using std::system;
using std::ostringstream;

namespace KalaCLI
{
	static vector<Command> commands{};

	vector<Command>& CommandManager::GetCommands() { return commands; }

	bool CommandManager::ParseCommand(const vector<string>& params)
	{
		if (params.empty()) return false;

		if (!COMMAND_PREFIX.empty()
			&& params[0].find(COMMAND_PREFIX.data()) == string::npos)
		{
			Log::Print(
				"Target command '" + params[0] + "' is missing required prefix '" + COMMAND_PREFIX.data() + "'!",
				"PARSE",
				LogType::LOG_ERROR,
				2);

				return false;
		}

		vector<string> cleanedParams = params;

		if (!COMMAND_PREFIX.empty()) cleanedParams[0] = RemoveFromString(cleanedParams[0], COMMAND_PREFIX.data());
		
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
				"Failed to run command '" + cleanedParams[0] + "' because it does not exist.",
				"PARSE",
				LogType::LOG_ERROR,
				2);
				
			return false;
		}

		foundCommand.targetFunction(cleanedParams);

		return true;
	}

	bool CommandManager::AddCommand(Command newValue)
	{
		if (newValue.primaryParam.empty())
		{
			Log::Print(
				"Skipped adding invalid command because it has no primary parameter!",
				"COMMAND",
				LogType::LOG_WARNING);

			return false;
		}
		if (!newValue.targetFunction)
		{
			Log::Print(
				"Skipped adding invalid command because it has no target function!",
				"COMMAND",
				LogType::LOG_WARNING);

			return false;
		}

		if (newValue.primaryParam.size() > 20)
		{
			Log::Print(
				"Skipped adding command with parameter '" + newValue.primaryParam + "' because it is too long.",
				"COMMAND",
				LogType::LOG_WARNING);

			return false;
		}

		//skip existing primary variants
		for (const auto& c : commands)
		{
			if (newValue.primaryParam == c.primaryParam)
			{
				Log::Print(
					"Skipped adding command with primary parameter '" + newValue.primaryParam + "' because it has already been used in another command.",
					"COMMAND",
					LogType::LOG_WARNING);

				return false;
			}
		}

		commands.push_back(newValue);

		return true;
	}
}