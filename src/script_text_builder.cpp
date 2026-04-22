#include "script_builder_internal.h"

namespace {

// ============================================================================
// Internal: Script templates (Linux + Windows pairs per file type)
// ============================================================================

constexpr auto CRLF = "\r\n"sv;

constexpr auto TOKEN_LINUX_FILENAME_ARG    = "{{LINUX_FILENAME_ARG}}"sv;
constexpr auto TOKEN_WINDOWS_FILENAME_ARG  = "{{WINDOWS_FILENAME_ARG}}"sv;
constexpr auto TOKEN_LINUX_ARGS            = "{{LINUX_ARGS}}"sv;
constexpr auto TOKEN_WINDOWS_ARGS          = "{{WINDOWS_ARGS}}"sv;
constexpr auto TOKEN_LINUX_ARGS_COMBINED   = "{{LINUX_ARGS_COMBINED}}"sv;
constexpr auto TOKEN_WINDOWS_ARGS_COMBINED = "{{WINDOWS_ARGS_COMBINED}}"sv;

#define LINUX_EXTRACT_ITEM R"(ITEM={{LINUX_FILENAME_ARG}};SELF=$(basename -- "$0");DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv -- "$0" "$DIR";cd "$DIR";unzip -qo -- "$SELF";)"
#define LINUX_EXTRACT_ITEM_HASH R"(ITEM={{LINUX_FILENAME_ARG}};SELF=$(basename -- "$0");DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv -- "$0" "$DIR";cd "$DIR";unzip -qo -- "$SELF";hash -r;)"
#define LINUX_EXTRACT_ITEM_HASH_NULL R"(ITEM={{LINUX_FILENAME_ARG}};SELF=$(basename -- "$0");DIR="pdvzip_extracted";NUL="/dev/null";clear;mkdir -p "$DIR";mv -- "$0" "$DIR";cd "$DIR";unzip -qo -- "$SELF";hash -r;)"
#define LINUX_EXTRACT_NO_ITEM R"(SELF=$(basename -- "$0");DIR="pdvzip_extracted";clear;mkdir -p "$DIR";mv -- "$0" "$DIR";cd "$DIR";unzip -qo -- "$SELF";)"
#define WINDOWS_EXTRACT R"(#&cls&setlocal EnableDelayedExpansion&set "DIR=pdvzip_extracted"&mkdir ".\!DIR!"&move "%~dpnx0" ".\!DIR!"&cd ".\!DIR!"&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&)"
#define WINDOWS_PYTHON_EXTRACT R"(#&cls&setlocal EnableDelayedExpansion&set "APP=python3"&set "DIR=pdvzip_extracted"&mkdir ".\!DIR!"&move "%~dpnx0" ".\!DIR!"&cd ".\!DIR!"&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&)"
#define WINDOWS_POWERSHELL_EXTRACT R"(#&cls&setlocal EnableDelayedExpansion&set "PDIR=%SystemDrive%\Program Files\PowerShell\"&set "DIR=pdvzip_extracted"&mkdir ".\!DIR!"&move "%~dpnx0" ".\!DIR!"&cd ".\!DIR!"&cls&tar -xf "%~n0%~x0"&ren "%~n0%~x0" *.png&)"

struct ScriptTemplate {
	std::string_view linux_part;
	std::string_view windows_part;
};

[[nodiscard]] ScriptTemplate getScriptTemplate(FileType file_type) {
	switch (file_type) {
		case FileType::VIDEO_AUDIO:
			return { std::string_view(LINUX_EXTRACT_ITEM_HASH_NULL R"(if command -v mpv >$NUL 2>&1;then clear;mpv --quiet --geometry=50%:50% "$ITEM" &> $NUL;elif command -v vlc >$NUL 2>&1;then clear;vlc --play-and-exit --no-video-title-show "$ITEM" &> $NUL;elif command -v firefox >$NUL 2>&1;then clear;firefox "$ITEM" &> $NUL;else clear;fi;exit;)"), std::string_view(WINDOWS_EXTRACT R"(start "" {{WINDOWS_FILENAME_ARG}}&exit)") };
		case FileType::PDF:
			return { std::string_view(LINUX_EXTRACT_ITEM_HASH_NULL R"(if command -v evince >$NUL 2>&1;then clear;evince "$ITEM" &> $NUL;else firefox "$ITEM" &> $NUL;clear;fi;exit;)"), std::string_view(WINDOWS_EXTRACT R"(start "" {{WINDOWS_FILENAME_ARG}}&exit)") };
		case FileType::PYTHON:
			return { std::string_view(LINUX_EXTRACT_ITEM_HASH R"(if command -v python3 >/dev/null 2>&1;then clear;python3 "$ITEM" {{LINUX_ARGS}};else clear;fi;exit;)"), std::string_view(WINDOWS_PYTHON_EXTRACT R"(where "!APP!" >nul 2>&1 && ("!APP!" {{WINDOWS_FILENAME_ARG}} {{WINDOWS_ARGS}} ) || (cls&exit)&echo.&exit)") };
		case FileType::POWERSHELL:
			return { std::string_view(LINUX_EXTRACT_ITEM_HASH R"(if command -v pwsh >/dev/null 2>&1;then clear;pwsh "$ITEM" {{LINUX_ARGS}};else clear;fi;exit;)"), std::string_view(WINDOWS_POWERSHELL_EXTRACT R"(IF EXIST "!PDIR!" (pwsh -ExecutionPolicy Bypass -File {{WINDOWS_FILENAME_ARG}} {{WINDOWS_ARGS}}&echo.&exit) ELSE (powershell -ExecutionPolicy Bypass -File {{WINDOWS_FILENAME_ARG}} {{WINDOWS_ARGS}}&echo.&exit))") };
		case FileType::BASH_SHELL:
			return { std::string_view(LINUX_EXTRACT_ITEM R"(chmod +x -- "$ITEM";"$ITEM" {{LINUX_ARGS}};exit;)"), std::string_view(WINDOWS_EXTRACT R"({{WINDOWS_FILENAME_ARG}} {{WINDOWS_ARGS}}&cls&exit)") };
		case FileType::WINDOWS_EXECUTABLE:
			return { std::string_view(LINUX_EXTRACT_NO_ITEM R"(clear;exit;)"), std::string_view(WINDOWS_EXTRACT R"({{WINDOWS_FILENAME_ARG}} {{WINDOWS_ARGS_COMBINED}}&echo.&exit)") };
		case FileType::FOLDER:
			return { std::string_view(LINUX_EXTRACT_ITEM R"(xdg-open "$ITEM" >/dev/null 2>&1;clear;exit;)"), std::string_view(WINDOWS_EXTRACT R"(start "" {{WINDOWS_FILENAME_ARG}}&cls&exit)") };
		case FileType::LINUX_EXECUTABLE:
			return { std::string_view(LINUX_EXTRACT_ITEM R"(chmod +x -- "$ITEM";"$ITEM" {{LINUX_ARGS_COMBINED}};exit;)"), std::string_view(WINDOWS_EXTRACT R"(cls&exit)") };
		case FileType::JAR:
			return { R"(clear;hash -r;if command -v java >/dev/null 2>&1;then clear;java -jar "$0" {{LINUX_ARGS}};else clear;fi;exit;)"sv, R"(#&cls&setlocal EnableDelayedExpansion&set "APP=java"&cls&where "!APP!" >nul 2>&1 && ("!APP!" -jar "%~dpnx0" {{WINDOWS_ARGS}} ) || (cls)&ren "%~dpnx0" *.png&echo.&exit)"sv };
		case FileType::UNKNOWN_FILE_TYPE:
		default:
			return { std::string_view(LINUX_EXTRACT_ITEM R"(xdg-open "$ITEM" >/dev/null 2>&1;exit;)"), std::string_view(WINDOWS_EXTRACT R"(start "" {{WINDOWS_FILENAME_ARG}}&echo.&exit)") };
	}
}

#undef LINUX_EXTRACT_ITEM
#undef LINUX_EXTRACT_ITEM_HASH
#undef LINUX_EXTRACT_ITEM_HASH_NULL
#undef LINUX_EXTRACT_NO_ITEM
#undef WINDOWS_EXTRACT
#undef WINDOWS_PYTHON_EXTRACT
#undef WINDOWS_POWERSHELL_EXTRACT

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
		return std::iscntrl(c) != 0;
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
		} else if (ch == '!') {
			// Inside double quotes with delayed expansion enabled,
			// '!' cannot be escaped by any means. Break out of the
			// quoted region, emit ^! (literal '!' outside quotes),
			// then immediately reopen the quoted region.
			out.append("\"^!\"");
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

std::string makePosixCommandPath(std::string_view path) {
	if (path.empty()) {
		throw std::runtime_error("Script Error: Archive filename is empty.");
	}

	std::string out("./");
	out.append(path);
	return out;
}

std::string makeWindowsCommandPath(std::string_view path) {
	if (path.empty()) {
		throw std::runtime_error("Script Error: Archive filename is empty.");
	}

	std::string out;
	out.reserve(path.size() + 2);
	out.append(".\\");
	for (char ch : path) {
		out.push_back(ch == '/' ? '\\' : ch);
	}
	return out;
}

template <typename SplitFn, typename QuoteFn>
std::string renderArguments(std::string_view raw_args, std::string_view field_name, SplitFn split, QuoteFn quote) {
	const auto args = split(raw_args, field_name);
	if (args.empty()) {
		return {};
	}

	std::string rendered;
	rendered.reserve(raw_args.size() * 2 + args.size());
	for (std::size_t i = 0; i < args.size(); ++i) {
		if (i > 0) {
			rendered.push_back(' ');
		}
		rendered.append(quote(args[i]));
	}
	return rendered;
}

std::string renderPosixArguments(std::string_view raw_args, std::string_view field_name) {
	return renderArguments(raw_args, field_name, splitPosixArguments, quotePosixArgument);
}

std::string renderWindowsArguments(std::string_view raw_args, std::string_view field_name) {
	return renderArguments(raw_args, field_name, splitWindowsArguments, quoteWindowsArgumentForCmd);
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

void rejectTemplateDelimiters(std::string_view value, std::string_view field_name) {
	if (value.find("{{") != std::string::npos) {
		throw std::runtime_error(std::format(
			"Script Error: {} contains reserved template delimiter '{{}}'.", field_name));
	}
}

}  // namespace

namespace script_builder_internal {

std::string buildScriptText(FileType file_type, const std::string& first_filename, const UserArguments& user_args) {
	validateScriptInput(first_filename, "Archive filename");
	validateScriptInput(user_args.linux_args, "Linux arguments");
	validateScriptInput(user_args.windows_args, "Windows arguments");

	// Reject values that contain template token delimiters to prevent
	// cross-token substitution (a filename like "{{LINUX_ARGS}}" would
	// otherwise be consumed by a later replaceAllInPlace pass).
	rejectTemplateDelimiters(first_filename, "Archive filename");
	rejectTemplateDelimiters(user_args.linux_args, "Linux arguments");
	rejectTemplateDelimiters(user_args.windows_args, "Windows arguments");

	const ScriptTemplate script_template = getScriptTemplate(file_type);

	std::string script_text;
	script_text.reserve(script_template.linux_part.size() + CRLF.size() + script_template.windows_part.size() + 256);
	script_text.append(script_template.linux_part);
	script_text.append(CRLF);
	script_text.append(script_template.windows_part);

	replaceAllInPlace(
		script_text,
		TOKEN_LINUX_FILENAME_ARG,
		quotePosixArgument(makePosixCommandPath(first_filename)));
	replaceAllInPlace(
		script_text,
		TOKEN_WINDOWS_FILENAME_ARG,
		quoteWindowsArgumentForCmd(makeWindowsCommandPath(first_filename)));

	replaceAllInPlace(
		script_text,
		TOKEN_LINUX_ARGS,
		renderPosixArguments(user_args.linux_args, "Linux arguments"));
	replaceAllInPlace(
		script_text,
		TOKEN_WINDOWS_ARGS,
		renderWindowsArguments(user_args.windows_args, "Windows arguments"));

	const std::string_view args_combined_raw = user_args.linux_args.empty()
		? std::string_view(user_args.windows_args)
		: std::string_view(user_args.linux_args);

	replaceAllInPlace(
		script_text,
		TOKEN_LINUX_ARGS_COMBINED,
		renderPosixArguments(args_combined_raw, "Combined Linux arguments"));
	replaceAllInPlace(
		script_text,
		TOKEN_WINDOWS_ARGS_COMBINED,
		renderWindowsArguments(args_combined_raw, "Combined Windows arguments"));

	ensureNoUnresolvedPlaceholders(script_text);
	return script_text;
}

}  // namespace script_builder_internal
