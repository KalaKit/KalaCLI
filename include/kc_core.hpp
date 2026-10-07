//Copyright(C) 2026 Lost Empire Entertainment
//This program comes with ABSOLUTELY NO WARRANTY.
//This is free software, and you are welcome to redistribute it under certain conditions.
//Read LICENSE.md for more information.

#pragma once

#include <string>
#include <vector>
#include <functional>

#include "core_utils.hpp"

namespace KalaCLI
{
	using std::string;
	using std::string_view;
	using std::vector;
	using std::pair;
	using std::function;
	
	//Prefix required in front of all commands
    static constexpr string_view COMMAND_PREFIX = "/";

    //How many newest lines to keep if console or external application keeps adding lines
    static constexpr u32 MAX_PAGE_LINES = 1000;

	//How many characters can be written into the text box
	static constexpr u32 MAX_INPUT_LENGTH = 1000;
    //How far back to store typed text history
	//after enter was pressed and text was cleared from input box
    static constexpr u32 MAX_TYPED_TEXT_HISTORY = 100;

    enum class PageDirection : u8
    {
        D_UP = 0,  //nth row from top
        D_DOWN = 1 //nth row from bottom
    };

    enum class TextRange : u8
    {
        R_ALL             = 0, //all content in page view box
        R_VISIBLE         = 1, //content currently visible in page view box
		R_HIGHLIGHTED_ROW = 2  //content within hovered row in page view
    };

	struct LIB_API Command
	{
		//What parameter to use to call this command
		string primaryParam{};

		//The description of this command that is listed when the built-in 'info' command is called
		string description{};

		//The function that contains the action this command will do,
		//must contain vector<string> as its only parameter to be able to receive user-passed parameters
		function<void(const vector<string>&)> targetFunction{};
	};

	class LIB_API KalaCLICore
	{
	public:
		static bool GetWrapState();
		//Toggles page view text wrapping on and off, defaults to true
		static void SetWrapState(bool state);

		//Resolve origin relative to target,
		//origin row is ignored if origin range is R_HIGHLIGHTED_ROW,
		//target range cannot be same as origin range or R_HIGHLIGHTED_ROW
		static u32 ResolveRow(
			TextRange originRange,
			u32 originRow,
			TextRange targetRange,
			u32 targetRow);

		//Returns max character count in page row
		static u32 GetRowSize();
		//Returns row count in page visible area
		static u32 GetRowCount();
		//Returns the global and visible range row which is currently highlighted,
		//returns { 0, 0 } if not in page view mode
		static pair<u32, u32> GetHighlightedRow();
		//Returns the column the cursor is currently at in page view
		static u32 GetHighlightedColumn();

		static bool CanConsoleWriteToPage();
		//Should new log messages be appended at the bottom of existing page content,
		//if disabled then console only allows to draw what the current page contains
		static void SetConsoleWritesToPageState(bool state);

		//Choose which rows from top and bottom count as scrollable area,
		//rows in between scroll range count as final scrollable area
		//so if outside rows are not overwritten once per frame then they will appear blank.
		//Set to { 0, 0 } to reset to full range.
		//Does not need to be called every frame, range scales dynamically with resize
		static void SetPageScrollRange(pair<u32, u32> range);

		//Choose what to display at the top bar above the table
		static void SetPageTitle(string_view title);

		//Returns all page content if text range is set to R_ALL,
		//otherwise returns page content within visible range
		static const vector<string>& GetPageContent(TextRange textRange = TextRange::R_ALL);
		//Decide what to display in the active page,
		//does not allow to exceed MAX_PAGE_LINES,
		//set to TextRange::R_VISIBLE to only update content visible in current page area,
		//same as overwrite row if text range is R_HIGHLIGHTED_ROW
		static void SetPageContent(
			const vector<string>& content,
			TextRange textRange = TextRange::R_ALL);

		//Creates a new row after the last row and writes to it,
		//or overwrites last row if row count is maxed out
		static void AppendToPage(string_view content);
		//Overwrites specific row within visible page area if text range is R_VISIBLE,
		//otherwise overwrites text within full page context.
		//Ignores page direction and target row if text range is R_HIGHLIGHTED_ROW.
		//If page direction is up then counts down from top row,
		//if page direction is down then counts up from bottom row,
		//target row is the row which will be overwritten.
		//Does not need to be called every frame unless content on the same row changes,
		//like for example if scroll range changes within page view box
		//and text range was set to R_VISIBLE or R_HIGHLIGHTED_ROW
		static void OverwriteRow(
			const string& content,
			TextRange textRange,
			PageDirection pageDirection,
			u32 targetRow);

		//Choose what to do when enter is pressed over a row while in page read mode,
		//Returns row, column and row text,
		//changing to empty restores original operation where it copies highlighted page view row to input field
		static void SetHighlightedRowEnterAction(function<void(u32, u32, string&)> action);

		//Choose what to do when a prefixless command is sent to the TUI,
		//great for things like chat TUI or server terminal,
		//changing to empty restores original operation where it simply cleaers input box
		static void SetPrefixlessInputAction(function<void(string&)> action);

		//Alternative to manually typing a command,
		//commands that start with TUI_COMMAND_PREFIX are sent to tui internal command parser,
		//writing a message without a command prefix writes it to the tui like a normal message.
		//Built in TUI commands:
		//- /help, /h: lists all available commands and what they do,
		//- /clear, /c: clears all tui page messages,
		//- /command command, /cmd command: sends selected message as command to console,
		//- /enableconsole, /ec: enables console-based updates,
		//- /disableconsole, /dc: disables console-based updates,
		//- /setpagetitle title, /spt title: updates page title,
		//- /exit, /e: close the tui
		static void SendCommand(string_view command);

		//Add a new command,
		//set target action to empty for existing command to remove it from commands list
		static void AddCommand(Command&& command);

		//Call once per frame,
		//draws current content,
		//updates values for GetRowCount, GetRowSize
		static void UpdateDisplayedContent();

		//Use this when you absolutely need a hard crash at this very moment.
		//Aborts and doesn't clean up data
		KNORETURN
		static void ForceClose(
			const string& title,
			const string& reason);
	};
}
