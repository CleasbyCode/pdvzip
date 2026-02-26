#include "pdvzip.h"

namespace {

// ============================================================================
// Internal: Script templates (Linux + Windows pairs per file type)
// ============================================================================

constexpr auto CRLF = "\r\n"sv;

constexpr auto TOKEN_LINUX_FILENAME_ARG   = "{{LINUX_FILENAME_ARG}}"sv;
constexpr auto TOKEN_WINDOWS_FILENAME_ARG = "{{WINDOWS_FILENAME_ARG}}"sv;
constexpr auto TOKEN_LINUX_ARGS           = "{{LINUX_ARGS}}"sv;
constexpr auto TOKEN_WINDOWS_ARGS         = "{{WINDOWS_ARGS}}"sv;
constexpr auto TOKEN_LINUX_ARGS_COMBINED  = "{{LINUX_ARGS_COMBINED}}"sv;
constexpr auto TOKEN_WINDOWS_ARGS_COMBINED = "{{WINDOWS_ARGS_COMBINED}}"sv;

struct ScriptTemplate {
	std::string_view linux_part;
	std::string_view windows_part;
};

const ScriptTemplate& getScriptTemplate(FileType file_type) {
	static const std::unordered_map<FileType, ScriptTemplate> TEMPLATES = {
		{
			FileType::VIDEO_AUDIO,
			{
				R"(ITEM={{LINUX_FILENAME_ARG}};DIR="pdvzip_extracted";NUL="/dev/null";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";hash -r;if command -v mpv >$NUL 2>&1;then clear;mpv --quiet --geometry=50%:50% "$ITEM" &> $NUL;elif command -v vlc >$NUL 2>&1;then clear;vlc --play-and-exit --no-video-title-show "$ITEM" &> $NUL;elif command -v firefox >$NUL 2>&1;then clear;firefox "$ITEM" &> $NUL;else clear;fi;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set "DIR=pdvzip_extracted"&mkdir ".\!DIR!"&move "%~dpnx0" ".\!DIR!"&cd ".\!DIR!"&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&start "" {{WINDOWS_FILENAME_ARG}}&exit)"sv
			}
		},
		{
			FileType::PDF,
			{
				R"(ITEM={{LINUX_FILENAME_ARG}};DIR="pdvzip_extracted";NUL="/dev/null";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";hash -r;if command -v evince >$NUL 2>&1;then clear;evince "$ITEM" &> $NUL;else firefox "$ITEM" &> $NUL;clear;fi;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set "DIR=pdvzip_extracted"&mkdir ".\!DIR!"&move "%~dpnx0" ".\!DIR!"&cd ".\!DIR!"&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&start "" {{WINDOWS_FILENAME_ARG}}&exit)"sv
			}
		},
		{
			FileType::PYTHON,
			{
				R"(ITEM={{LINUX_FILENAME_ARG}};DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";hash -r;if command -v python3 >/dev/null 2>&1;then clear;python3 "$ITEM" {{LINUX_ARGS}};else clear;fi;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set "APP=python3"&set "DIR=pdvzip_extracted"&mkdir ".\!DIR!"&move "%~dpnx0" ".\!DIR!"&cd ".\!DIR!"&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&where "!APP!" >nul 2>&1 && ("!APP!" {{WINDOWS_FILENAME_ARG}} {{WINDOWS_ARGS}} ) || (cls&exit)&echo.&exit)"sv
			}
		},
		{
			FileType::POWERSHELL,
			{
				R"(DIR="pdvzip_extracted";ITEM={{LINUX_FILENAME_ARG}};clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";hash -r;if command -v pwsh >/dev/null 2>&1;then clear;pwsh "$ITEM" {{LINUX_ARGS}};else clear;fi;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set "PDIR=%SystemDrive%\Program Files\PowerShell\"&set "DIR=pdvzip_extracted"&mkdir ".\!DIR!"&move "%~dpnx0" ".\!DIR!"&cd ".\!DIR!"&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&IF EXIST "!PDIR!" (pwsh -ExecutionPolicy Bypass -File {{WINDOWS_FILENAME_ARG}} {{WINDOWS_ARGS}}&echo.&exit) ELSE (powershell -ExecutionPolicy Bypass -File {{WINDOWS_FILENAME_ARG}} {{WINDOWS_ARGS}}&echo.&exit))"sv
			}
		},
		{
			FileType::BASH_SHELL,
			{
				R"(ITEM={{LINUX_FILENAME_ARG}};DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";chmod +x "$ITEM";./"$ITEM" {{LINUX_ARGS}};exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set "DIR=pdvzip_extracted"&mkdir ".\!DIR!"&move "%~dpnx0" ".\!DIR!"&cd ".\!DIR!"&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&{{WINDOWS_FILENAME_ARG}} {{WINDOWS_ARGS}}&cls&exit)"sv
			}
		},
		{
			FileType::WINDOWS_EXECUTABLE,
			{
				R"(DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";clear;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set "DIR=pdvzip_extracted"&mkdir ".\!DIR!"&move "%~dpnx0" ".\!DIR!"&cd ".\!DIR!"&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&{{WINDOWS_FILENAME_ARG}} {{WINDOWS_ARGS_COMBINED}}&echo.&exit)"sv
			}
		},
		{
			FileType::FOLDER,
			{
				R"(ITEM={{LINUX_FILENAME_ARG}};DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";xdg-open "$ITEM" &> /dev/null;clear;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set "DIR=pdvzip_extracted"&mkdir ".\!DIR!"&move "%~dpnx0" ".\!DIR!"&cd ".\!DIR!"&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&powershell "II '{{WINDOWS_FILENAME_ARG}}'"&cls&exit)"sv
			}
		},
		{
			FileType::LINUX_EXECUTABLE,
			{
				R"(ITEM={{LINUX_FILENAME_ARG}};DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";chmod +x "$ITEM";./"$ITEM" {{LINUX_ARGS_COMBINED}};exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set "DIR=pdvzip_extracted"&mkdir ".\!DIR!"&move "%~dpnx0" ".\!DIR!"&cd ".\!DIR!"&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&cls&exit)"sv
			}
		},
		{
			FileType::JAR,
			{
				R"(clear;hash -r;if command -v java >/dev/null 2>&1;then clear;java -jar "$0" {{LINUX_ARGS}};else clear;fi;exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set "APP=java"&cls&where "!APP!" >nul 2>&1 && ("!APP!" -jar "%~dpnx0" {{WINDOWS_ARGS}} ) || (cls)&ren "%~dpnx0" *.png&echo.&exit)"sv
			}
		},
		{
			FileType::UNKNOWN_FILE_TYPE,
			{
				R"(ITEM={{LINUX_FILENAME_ARG}};DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv "$0" "$DIR";cd "$DIR";unzip -qo "$0";xdg-open "$ITEM";exit;)"sv,
				R"(#&cls&setlocal EnableDelayedExpansion&set "DIR=pdvzip_extracted"&mkdir ".\!DIR!"&move "%~dpnx0" ".\!DIR!"&cd ".\!DIR!"&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&start "" {{WINDOWS_FILENAME_ARG}}&echo.&exit)"sv
			}
		},
	};

	if (const auto it = TEMPLATES.find(file_type); it != TEMPLATES.end()) {
		return it->second;
	}
	return TEMPLATES.at(FileType::UNKNOWN_FILE_TYPE);
}

void replaceAllInPlace(std::string& target, std::string_view from, std::string_view to) {
	if (from.empty()) {
		return;
	}

	std::size_t position = 0;
	while ((position = target.find(from, position)) != std::string::npos) {
		target.replace(position, from.size(), to);
		position += to.size();
	}
}

void validateScriptInput(std::string_view value, std::string_view field_name) {
	const bool has_control_char = std::ranges::any_of(value, [](unsigned char c) {
		return c == '\0' || c == '\n' || c == '\r';
	});
	if (has_control_char) {
		throw std::runtime_error(std::format(
			"Arguments Error: {} contains unsupported control characters.", field_name));
	}
}

std::vector<std::string> splitPosixArguments(std::string_view input, std::string_view field_name) {
	enum class QuoteState : Byte {
		none,
		single,
		double_quote
	};

	auto syntaxError = [&](std::string_view reason) {
		throw std::runtime_error(std::format(
			"Arguments Error: {} {}",
			field_name, reason));
	};

	std::vector<std::string> args;
	std::string current;
	QuoteState state = QuoteState::none;
	bool escaped = false;
	bool token_started = false;

	for (char ch : input) {
		if (state == QuoteState::single) {
			if (ch == '\'') {
				state = QuoteState::none;
			} else {
				current.push_back(ch);
			}
			token_started = true;
			continue;
		}

		if (escaped) {
			current.push_back(ch);
			escaped = false;
			token_started = true;
			continue;
		}

		if (state == QuoteState::double_quote) {
			if (ch == '"') {
				state = QuoteState::none;
			} else if (ch == '\\') {
				escaped = true;
			} else {
				current.push_back(ch);
			}
			token_started = true;
			continue;
		}

		if (std::isspace(static_cast<unsigned char>(ch))) {
			if (token_started) {
				args.push_back(current);
				current.clear();
				token_started = false;
			}
			continue;
		}

		switch (ch) {
			case '\\':
				escaped = true;
				token_started = true;
				break;
			case '\'':
				state = QuoteState::single;
				token_started = true;
				break;
			case '"':
				state = QuoteState::double_quote;
				token_started = true;
				break;
			default:
				current.push_back(ch);
				token_started = true;
				break;
		}
	}

	if (escaped) {
		syntaxError("end with an unfinished escape sequence.");
	}
	if (state != QuoteState::none) {
		syntaxError("contain unmatched quotes.");
	}
	if (token_started) {
		args.push_back(current);
	}

	return args;
}

std::vector<std::string> splitWindowsArguments(std::string_view input, std::string_view field_name) {
	auto syntaxError = [&](std::string_view reason) {
		throw std::runtime_error(std::format(
			"Arguments Error: {} {}",
			field_name, reason));
	};

	std::vector<std::string> args;
	std::size_t i = 0;

	while (i < input.size()) {
		while (i < input.size() && std::isspace(static_cast<unsigned char>(input[i]))) {
			++i;
		}
		if (i >= input.size()) {
			break;
		}

		std::string current;
		bool in_quotes = false;
		std::size_t backslashes = 0;

		while (i < input.size()) {
			const char ch = input[i];

			if (ch == '\\') {
				++backslashes;
				++i;
				continue;
			}

			if (ch == '"') {
				if ((backslashes % 2) == 0) {
					current.append(backslashes / 2, '\\');
					backslashes = 0;

					if (in_quotes && (i + 1) < input.size() && input[i + 1] == '"') {
						current.push_back('"');
						i += 2;
						continue;
					}

					in_quotes = !in_quotes;
					++i;
					continue;
				}

				current.append(backslashes / 2, '\\');
				current.push_back('"');
				backslashes = 0;
				++i;
				continue;
			}

			if (backslashes > 0) {
				current.append(backslashes, '\\');
				backslashes = 0;
			}

			if (!in_quotes && std::isspace(static_cast<unsigned char>(ch))) {
				break;
			}

			current.push_back(ch);
			++i;
		}

		if (backslashes > 0) {
			current.append(backslashes, '\\');
		}
		if (in_quotes) {
			syntaxError("contain unmatched double quotes.");
		}

		args.push_back(std::move(current));

		while (i < input.size() && std::isspace(static_cast<unsigned char>(input[i]))) {
			++i;
		}
	}

	return args;
}

std::string quotePosixArgument(std::string_view arg) {
	std::string out;
	out.reserve(arg.size() + 2);
	out.push_back('\'');

	for (char ch : arg) {
		if (ch == '\'') {
			out.append("'\\''");
		} else {
			out.push_back(ch);
		}
	}

	out.push_back('\'');
	return out;
}

std::string quoteWindowsArgumentForCmd(std::string_view arg) {
	std::string out;
	out.reserve(arg.size() * 2 + 2);
	out.push_back('"');

	std::size_t backslashes = 0;
	for (char ch : arg) {
		if (ch == '\\') {
			++backslashes;
			continue;
		}

		if (ch == '"') {
			out.append(backslashes * 2 + 1, '\\');
			out.push_back('"');
			backslashes = 0;
			continue;
		}

		if (backslashes > 0) {
			out.append(backslashes, '\\');
			backslashes = 0;
		}

		// Prevent percent-expansion in CMD (including inside quoted args).
		if (ch == '%') {
			out.append("%%");
		} else {
			out.push_back(ch);
		}
	}

	if (backslashes > 0) {
		out.append(backslashes * 2, '\\');
	}

	out.push_back('"');
	return out;
}

std::string renderPosixArguments(std::string_view raw_args, std::string_view field_name) {
	const auto args = splitPosixArguments(raw_args, field_name);
	if (args.empty()) {
		return {};
	}

	std::string rendered;
	for (std::size_t i = 0; i < args.size(); ++i) {
		if (i > 0) {
			rendered.push_back(' ');
		}
		rendered.append(quotePosixArgument(args[i]));
	}
	return rendered;
}

std::string renderWindowsArguments(std::string_view raw_args, std::string_view field_name) {
	const auto args = splitWindowsArguments(raw_args, field_name);
	if (args.empty()) {
		return {};
	}

	std::string rendered;
	for (std::size_t i = 0; i < args.size(); ++i) {
		if (i > 0) {
			rendered.push_back(' ');
		}
		rendered.append(quoteWindowsArgumentForCmd(args[i]));
	}
	return rendered;
}

void ensureNoUnresolvedPlaceholders(std::string_view script_text) {
	constexpr auto TOKENS = std::to_array<std::string_view>({
		TOKEN_LINUX_FILENAME_ARG,
		TOKEN_WINDOWS_FILENAME_ARG,
		TOKEN_LINUX_ARGS,
		TOKEN_WINDOWS_ARGS,
		TOKEN_LINUX_ARGS_COMBINED,
		TOKEN_WINDOWS_ARGS_COMBINED
	});

	for (const std::string_view token : TOKENS) {
		if (script_text.find(token) != std::string::npos) {
			throw std::runtime_error("Script Error: Unresolved placeholder token in extraction script template.");
		}
	}
}

std::string buildScriptText(FileType file_type, const std::string& first_filename, const UserArguments& user_args) {
	const ScriptTemplate& script_template = getScriptTemplate(file_type);

	std::string script_text;
	script_text.reserve(script_template.linux_part.size() + CRLF.size() + script_template.windows_part.size() + 256);
	script_text.append(script_template.linux_part);
	script_text.append(CRLF);
	script_text.append(script_template.windows_part);

	if (script_text.find(TOKEN_LINUX_FILENAME_ARG) != std::string::npos) {
		replaceAllInPlace(script_text, TOKEN_LINUX_FILENAME_ARG, quotePosixArgument(first_filename));
	}
	if (script_text.find(TOKEN_WINDOWS_FILENAME_ARG) != std::string::npos) {
		replaceAllInPlace(script_text, TOKEN_WINDOWS_FILENAME_ARG, quoteWindowsArgumentForCmd(first_filename));
	}

	if (script_text.find(TOKEN_LINUX_ARGS) != std::string::npos) {
		replaceAllInPlace(
			script_text,
			TOKEN_LINUX_ARGS,
			renderPosixArguments(user_args.linux_args, "Linux arguments"));
	}
	if (script_text.find(TOKEN_WINDOWS_ARGS) != std::string::npos) {
		replaceAllInPlace(
			script_text,
			TOKEN_WINDOWS_ARGS,
			renderWindowsArguments(user_args.windows_args, "Windows arguments"));
	}

	const std::string_view args_combined_raw = user_args.linux_args.empty()
		? std::string_view(user_args.windows_args)
		: std::string_view(user_args.linux_args);

	if (script_text.find(TOKEN_LINUX_ARGS_COMBINED) != std::string::npos) {
		replaceAllInPlace(
			script_text,
			TOKEN_LINUX_ARGS_COMBINED,
			renderPosixArguments(args_combined_raw, "Combined Linux arguments"));
	}
	if (script_text.find(TOKEN_WINDOWS_ARGS_COMBINED) != std::string::npos) {
		replaceAllInPlace(
			script_text,
			TOKEN_WINDOWS_ARGS_COMBINED,
			renderWindowsArguments(args_combined_raw, "Combined Windows arguments"));
	}

	ensureNoUnresolvedPlaceholders(script_text);
	return script_text;
}

} // anonymous namespace

// ============================================================================
// Public: Build the extraction script chunk (iCCP)
// ============================================================================

vBytes buildExtractionScript(FileType file_type, const std::string& first_filename,
                             const UserArguments& user_args) {

	constexpr std::size_t
		SCRIPT_INDEX            = 0x16,
		ICCP_CHUNK_NAME_INDEX   = 0x04,
		ICCP_CHUNK_NAME_LENGTH  = 4,
		ICCP_CRC_INDEX_DIFF     = 8,
		LENGTH_FIRST_BYTE_INDEX = 3;

	validateScriptInput(first_filename, "Archive filename");
	validateScriptInput(user_args.linux_args, "Linux arguments");
	validateScriptInput(user_args.windows_args, "Windows arguments");

	// iCCP chunk header skeleton.
	vBytes script_vec {
		0x00, 0x00, 0x00, 0x00, 0x69, 0x43, 0x43, 0x50,
		0x44, 0x56, 0x5A, 0x49, 0x50, 0x5F, 0x5F, 0x00,
		0x00, 0x0D, 0x52, 0x45, 0x4D, 0x3B, 0x0D, 0x0A,
		0x00, 0x00, 0x00, 0x00
	};
	script_vec.reserve(script_vec.size() + MAX_SCRIPT_SIZE);

	const std::string script_text = buildScriptText(file_type, first_filename, user_args);
	script_vec.insert(
		script_vec.begin() + SCRIPT_INDEX,
		script_text.begin(),
		script_text.end());

	// Update the iCCP chunk length field.
	constexpr std::size_t
		LENGTH_INDEX = 0,
		VALUE_LENGTH = 4;

	std::size_t chunk_data_size = script_vec.size() - CHUNK_FIELDS_COMBINED_LENGTH;
	updateValue(script_vec, LENGTH_INDEX, static_cast<uint32_t>(chunk_data_size), VALUE_LENGTH);

	// If the first byte of the chunk length is a problematic metacharacter for
	// the Linux extraction script, pad the chunk to shift past it.
	if (std::ranges::contains(LINUX_PROBLEM_METACHARACTERS, script_vec[LENGTH_FIRST_BYTE_INDEX])) {
		constexpr std::string_view PAD = "........";
		constexpr std::size_t PAD_OFFSET = 8;
		script_vec.insert(
			script_vec.begin() + chunk_data_size + PAD_OFFSET,
			PAD.begin(), PAD.end());
		chunk_data_size = script_vec.size() - CHUNK_FIELDS_COMBINED_LENGTH;
		updateValue(script_vec, LENGTH_INDEX, static_cast<uint32_t>(chunk_data_size), VALUE_LENGTH);
	}

	if (chunk_data_size > MAX_SCRIPT_SIZE) {
		throw std::runtime_error("Script Size Error: Extraction script exceeds size limit.");
	}

	// Compute and write the CRC for the iCCP chunk.
	const std::size_t iccp_chunk_length = chunk_data_size + ICCP_CHUNK_NAME_LENGTH;
	const uint32_t crc = lodepng_crc32(&script_vec[ICCP_CHUNK_NAME_INDEX], iccp_chunk_length);
	const std::size_t crc_index = chunk_data_size + ICCP_CRC_INDEX_DIFF;

	updateValue(script_vec, crc_index, crc, VALUE_LENGTH);
	return script_vec;
}
