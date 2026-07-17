#include "script_builder_internal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <stdexcept>

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

#define LINUX_EXTRACT_SETUP R"(SELF=${0##*/};DIR=${SELF%.*};case $DIR in ''|.|..|"$SELF")DIR=${SELF}_files;;esac;PATH_DIR=./$DIR;)"
#define LINUX_EXTRACT_ITEM R"(ITEM={{LINUX_FILENAME_ARG}};)" LINUX_EXTRACT_SETUP R"(clear;mkdir -- "$PATH_DIR"||exit;mv -- "$0" "$PATH_DIR"||exit;cd "$PATH_DIR"||exit;unzip -qo -- "$SELF"||exit;)"
#define LINUX_EXTRACT_ITEM_HASH R"(ITEM={{LINUX_FILENAME_ARG}};)" LINUX_EXTRACT_SETUP R"(clear;mkdir -- "$PATH_DIR"||exit;mv -- "$0" "$PATH_DIR"||exit;cd "$PATH_DIR"||exit;unzip -qo -- "$SELF"||exit;hash -r;)"
#define LINUX_EXTRACT_ITEM_HASH_NULL R"(ITEM={{LINUX_FILENAME_ARG}};)" LINUX_EXTRACT_SETUP R"(NUL="/dev/null";clear;mkdir -- "$PATH_DIR"||exit;mv -- "$0" "$PATH_DIR"||exit;cd "$PATH_DIR"||exit;unzip -qo -- "$SELF"||exit;hash -r;)"
#define LINUX_EXTRACT_NO_ITEM LINUX_EXTRACT_SETUP R"(clear;mkdir -- "$PATH_DIR"||exit;mv -- "$0" "$PATH_DIR"||exit;cd "$PATH_DIR"||exit;unzip -qo -- "$SELF"||exit;)"
// CMD reads a batch file incrementally. WINDOWS_RESTORE first unwinds that
// batch context, then moves the polyglot using commands already in the parser.
#define WINDOWS_BASE \
	R"(#&cls&@echo off&setlocal EnableExtensions DisableDelayedExpansion)" "\r\n" \
	R"(set "ERRORLEVEL=")" "\r\n"
#define WINDOWS_EXTRACT \
	WINDOWS_BASE \
	R"(set "DIR=%~n0")" "\r\n" \
	R"(mkdir ".\%DIR%"||exit /b)" "\r\n" \
	R"(cd ".\%DIR%"||exit /b)" "\r\n" \
	R"(for %%I in (".\%~n0.png") do set "PDVZIP_RESTORE_TARGET=%%~fI")" "\r\n" \
	R"(cls&tar -xf "%~dpnx0"||exit /b)" "\r\n"
#define WINDOWS_POWERSHELL_EXTRACT \
	WINDOWS_BASE \
	R"(set "APP=")" "\r\n" \
	R"(set "DIR=%~n0")" "\r\n" \
	R"(mkdir ".\%DIR%"||exit /b)" "\r\n" \
	R"(cd ".\%DIR%"||exit /b)" "\r\n" \
	R"(for %%I in (".\%~n0.png") do set "PDVZIP_RESTORE_TARGET=%%~fI")" "\r\n" \
	R"(cls&tar -xf "%~dpnx0"||exit /b)" "\r\n"
#define WINDOWS_RESTORE \
	R"(:PDVZIP_RESTORE)" "\r\n" \
	R"(if exist "%PDVZIP_RESTORE_TARGET%" (>&2 echo pdvzip: PNG restore target already exists.&exit /b 1))" "\r\n" \
	R"((goto) 2>nul&move /-Y "%~f0" "%PDVZIP_RESTORE_TARGET%" <nul >nul&if errorlevel 1 ("%ComSpec%" /d /c exit 1) else ("%ComSpec%" /d /c exit %STATUS%))" "\r\n"
#define WINDOWS_JAR_RESTORE \
	R"(:PDVZIP_RESTORE)" "\r\n" \
	R"(if exist "%~dpn0.png" (>&2 echo pdvzip: PNG restore target already exists.&exit /b 1))" "\r\n" \
	R"((goto) 2>nul&ren "%~f0" "%~n0.png" >nul&if errorlevel 1 ("%ComSpec%" /d /c exit 1) else ("%ComSpec%" /d /c exit %STATUS%))" "\r\n"

struct ScriptTemplate {
	std::string_view linux_part;
	std::string_view windows_part;
};

struct PlaceholderReplacement {
	std::string_view token;
	std::string value;
};

[[nodiscard]] ScriptTemplate getScriptTemplate(FileType file_type) {
	switch (file_type) {
		case FileType::VIDEO_AUDIO:
			return { std::string_view(LINUX_EXTRACT_ITEM_HASH_NULL R"(if command -v mpv >$NUL 2>&1;then clear;mpv --quiet --geometry=50%:50% "$ITEM" &> $NUL;elif command -v vlc >$NUL 2>&1;then clear;vlc --play-and-exit --no-video-title-show "$ITEM" &> $NUL;elif command -v firefox >$NUL 2>&1;then clear;firefox "$ITEM" &> $NUL;else clear;fi;exit;)"), std::string_view(WINDOWS_EXTRACT R"(start "" {{WINDOWS_FILENAME_ARG}})" "\r\n" R"(set "STATUS=%ERRORLEVEL%")" "\r\n" WINDOWS_RESTORE) };
		case FileType::PDF:
			return { std::string_view(LINUX_EXTRACT_ITEM_HASH_NULL R"(if command -v evince >$NUL 2>&1;then clear;evince "$ITEM" &> $NUL;else firefox "$ITEM" &> $NUL;clear;fi;exit;)"), std::string_view(WINDOWS_EXTRACT R"(start "" {{WINDOWS_FILENAME_ARG}})" "\r\n" R"(set "STATUS=%ERRORLEVEL%")" "\r\n" WINDOWS_RESTORE) };
		case FileType::PYTHON:
			return { std::string_view(LINUX_EXTRACT_ITEM_HASH R"(if command -v python3 >/dev/null 2>&1;then clear;python3 "$ITEM" {{LINUX_ARGS}};STATUS=$?;exit "$STATUS";else clear;printf '%s\n' 'pdvzip: required runtime python3 was not found.' >&2;exit 127;fi;)"), std::string_view(WINDOWS_EXTRACT R"(where python3 >nul 2>&1)" "\r\n" R"(if errorlevel 1 (>&2 echo pdvzip: required runtime python3 was not found.&set "STATUS=127"&goto :PDVZIP_RESTORE))" "\r\n" R"(python3 {{WINDOWS_FILENAME_ARG}} {{WINDOWS_ARGS}})" "\r\n" R"(set "STATUS=%ERRORLEVEL%")" "\r\n" WINDOWS_RESTORE) };
		case FileType::POWERSHELL:
			return { std::string_view(LINUX_EXTRACT_ITEM_HASH R"(if command -v pwsh >/dev/null 2>&1;then clear;pwsh "$ITEM" {{LINUX_ARGS}};STATUS=$?;exit "$STATUS";else clear;printf '%s\n' 'pdvzip: required runtime pwsh was not found.' >&2;exit 127;fi;)"), std::string_view(WINDOWS_POWERSHELL_EXTRACT R"(where pwsh >nul 2>&1)" "\r\n" R"(if not errorlevel 1 set "APP=pwsh")" "\r\n" R"(if not defined APP where powershell >nul 2>&1)" "\r\n" R"(if not defined APP if not errorlevel 1 set "APP=powershell")" "\r\n" R"(if not defined APP (>&2 echo pdvzip: required PowerShell runtime was not found.&set "STATUS=127"&goto :PDVZIP_RESTORE))" "\r\n" R"(%APP% -ExecutionPolicy Bypass -File {{WINDOWS_FILENAME_ARG}} {{WINDOWS_ARGS}})" "\r\n" R"(set "STATUS=%ERRORLEVEL%")" "\r\n" WINDOWS_RESTORE) };
		case FileType::BASH_SHELL:
			return { std::string_view(LINUX_EXTRACT_ITEM R"(chmod +x -- "$ITEM";"$ITEM" {{LINUX_ARGS}};exit;)"), std::string_view(WINDOWS_EXTRACT R"({{WINDOWS_FILENAME_ARG}} {{WINDOWS_ARGS}})" "\r\n" R"(set "STATUS=%ERRORLEVEL%")" "\r\n" R"(cls)" "\r\n" WINDOWS_RESTORE) };
		case FileType::WINDOWS_EXECUTABLE:
			return { std::string_view(LINUX_EXTRACT_NO_ITEM R"(clear;exit;)"), std::string_view(WINDOWS_EXTRACT R"({{WINDOWS_FILENAME_ARG}} {{WINDOWS_ARGS_COMBINED}})" "\r\n" R"(set "STATUS=%ERRORLEVEL%")" "\r\n" R"(echo.)" "\r\n" WINDOWS_RESTORE) };
		case FileType::FOLDER:
			return { std::string_view(LINUX_EXTRACT_ITEM R"(xdg-open "$ITEM" >/dev/null 2>&1;clear;exit;)"), std::string_view(WINDOWS_EXTRACT R"(start "" {{WINDOWS_FILENAME_ARG}})" "\r\n" R"(set "STATUS=%ERRORLEVEL%")" "\r\n" R"(cls)" "\r\n" WINDOWS_RESTORE) };
		case FileType::LINUX_EXECUTABLE:
			return { std::string_view(LINUX_EXTRACT_ITEM R"(chmod +x -- "$ITEM";"$ITEM" {{LINUX_ARGS_COMBINED}};exit;)"), std::string_view(WINDOWS_EXTRACT R"(cls)" "\r\n" R"(set "STATUS=0")" "\r\n" WINDOWS_RESTORE) };
		case FileType::JAR:
			return { R"(clear;hash -r;if command -v java >/dev/null 2>&1;then clear;java -jar "$0" {{LINUX_ARGS}};STATUS=$?;exit "$STATUS";else clear;printf '%s\n' 'pdvzip: required runtime java was not found.' >&2;exit 127;fi;)"sv, std::string_view(WINDOWS_BASE R"(where java >nul 2>&1)" "\r\n" R"(if errorlevel 1 (>&2 echo pdvzip: required runtime java was not found.&set "STATUS=127"&goto :PDVZIP_RESTORE))" "\r\n" R"(java -jar "%~dpnx0" {{WINDOWS_ARGS}})" "\r\n" R"(set "STATUS=%ERRORLEVEL%")" "\r\n" WINDOWS_JAR_RESTORE) };
		case FileType::UNKNOWN_FILE_TYPE:
		default:
			return { std::string_view(LINUX_EXTRACT_ITEM R"(xdg-open "$ITEM" >/dev/null 2>&1;exit;)"), std::string_view(WINDOWS_EXTRACT R"(start "" {{WINDOWS_FILENAME_ARG}})" "\r\n" R"(set "STATUS=%ERRORLEVEL%")" "\r\n" R"(echo.)" "\r\n" WINDOWS_RESTORE) };
	}
}

#undef LINUX_EXTRACT_ITEM
#undef LINUX_EXTRACT_ITEM_HASH
#undef LINUX_EXTRACT_ITEM_HASH_NULL
#undef LINUX_EXTRACT_NO_ITEM
#undef LINUX_EXTRACT_SETUP
#undef WINDOWS_BASE
#undef WINDOWS_EXTRACT
#undef WINDOWS_POWERSHELL_EXTRACT
#undef WINDOWS_RESTORE
#undef WINDOWS_JAR_RESTORE

void validateScriptInput(std::string_view value, std::string_view field_name) {
	const bool has_control_char = std::ranges::any_of(value, [](unsigned char c) {
		return std::iscntrl(c) != 0;
	});
	if (has_control_char) {
		throw std::runtime_error(std::format(
			"Arguments Error: {} contains unsupported control characters.", field_name));
	}
}

void rejectWindowsCommandQuotes(std::string_view value, std::string_view field_name) {
	if (value.find('"') != std::string_view::npos) {
		throw std::runtime_error(std::format(
			"Script Error: {} contains a literal double quote, which cannot be safely "
			"embedded in a Windows command.",
			field_name));
	}
}

}  // namespace (templates and validation stay internal)

namespace script_builder_internal {

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
	// Backslash does not escape a quote from cmd.exe's command parser.  A
	// CRT-style encoding such as \" therefore lets the quote terminate this
	// argument before metacharacters that follow it.  Keep the encoder's
	// contract deliberately narrow until a CMD-aware literal-quote encoding is
	// available.
	rejectWindowsCommandQuotes(arg, "Windows command value");

	std::string out;
	out.reserve(arg.size() * 2 + 2);
	out.push_back('"');

	std::size_t backslashes = 0;
	for (char ch : arg) {
		if (ch == '\\') {
			++backslashes;
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

}  // namespace script_builder_internal

namespace {

using script_builder_internal::quotePosixArgument;
using script_builder_internal::quoteWindowsArgumentForCmd;
using script_builder_internal::splitPosixArguments;
using script_builder_internal::splitWindowsArguments;

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

std::string renderTemplate(std::string_view template_text, std::span<const PlaceholderReplacement> replacements) {
	std::string rendered;
	rendered.reserve(template_text.size() + 256);

	std::size_t position = 0;
	while (position < template_text.size()) {
		const std::size_t marker = template_text.find("{{", position);
		if (marker == std::string_view::npos) {
			rendered.append(template_text.substr(position));
			break;
		}

		rendered.append(template_text.substr(position, marker - position));

		bool matched = false;
		for (const PlaceholderReplacement& replacement : replacements) {
			if (template_text.substr(marker, replacement.token.size()) == replacement.token) {
				rendered.append(replacement.value);
				position = marker + replacement.token.size();
				matched = true;
				break;
			}
		}

		if (!matched) {
			throw std::runtime_error("Script Error: Unknown placeholder token in extraction script template.");
		}
	}

	return rendered;
}

void validateReplacementInput(std::string_view value, std::string_view field_name) {
	validateScriptInput(value, field_name);

	// Reject values that contain template token delimiters, so user input cannot
	// impersonate a placeholder while the template is rendered.
	rejectTemplateDelimiters(value, field_name);
}

[[nodiscard]] std::string_view combinedLinuxArgumentsRaw(const UserArguments& user_args) {
	return user_args.linux_args.empty()
		? std::string_view(user_args.windows_args)
		: std::string_view(user_args.linux_args);
}

[[nodiscard]] std::string_view combinedWindowsArgumentsRaw(const UserArguments& user_args) {
	return user_args.windows_args.empty()
		? std::string_view(user_args.linux_args)
		: std::string_view(user_args.windows_args);
}

[[nodiscard]] std::vector<PlaceholderReplacement> makePlaceholderReplacements(
	std::string_view template_text,
	const std::string& first_filename,
	const UserArguments& user_args
) {
	std::vector<PlaceholderReplacement> replacements;
	replacements.reserve(6);

	if (template_text.find(TOKEN_LINUX_FILENAME_ARG) != std::string_view::npos) {
		validateReplacementInput(first_filename, "Archive filename");
		replacements.push_back({
			TOKEN_LINUX_FILENAME_ARG,
			quotePosixArgument(makePosixCommandPath(first_filename))
		});
	}
	if (template_text.find(TOKEN_WINDOWS_FILENAME_ARG) != std::string_view::npos) {
		validateReplacementInput(first_filename, "Archive filename");
		rejectWindowsCommandQuotes(first_filename, "Archive filename");
		replacements.push_back({
			TOKEN_WINDOWS_FILENAME_ARG,
			quoteWindowsArgumentForCmd(makeWindowsCommandPath(first_filename))
		});
	}
	if (template_text.find(TOKEN_LINUX_ARGS) != std::string_view::npos) {
		validateReplacementInput(user_args.linux_args, "Linux arguments");
		replacements.push_back({
			TOKEN_LINUX_ARGS,
			renderPosixArguments(user_args.linux_args, "Linux arguments")
		});
	}
	if (template_text.find(TOKEN_WINDOWS_ARGS) != std::string_view::npos) {
		validateReplacementInput(user_args.windows_args, "Windows arguments");
		replacements.push_back({
			TOKEN_WINDOWS_ARGS,
			renderWindowsArguments(user_args.windows_args, "Windows arguments")
		});
	}
	if (template_text.find(TOKEN_LINUX_ARGS_COMBINED) != std::string_view::npos) {
		const std::string_view raw_args = combinedLinuxArgumentsRaw(user_args);
		validateReplacementInput(raw_args, "Combined Linux arguments");
		replacements.push_back({
			TOKEN_LINUX_ARGS_COMBINED,
			renderPosixArguments(raw_args, "Combined Linux arguments")
		});
	}
	if (template_text.find(TOKEN_WINDOWS_ARGS_COMBINED) != std::string_view::npos) {
		const std::string_view raw_args = combinedWindowsArgumentsRaw(user_args);
		validateReplacementInput(raw_args, "Combined Windows arguments");
		replacements.push_back({
			TOKEN_WINDOWS_ARGS_COMBINED,
			renderWindowsArguments(raw_args, "Combined Windows arguments")
		});
	}

	return replacements;
}

[[nodiscard]] std::string joinScriptTemplate(const ScriptTemplate& script_template) {
	std::string template_text;
	template_text.reserve(script_template.linux_part.size() + CRLF.size() + script_template.windows_part.size());
	template_text.append(script_template.linux_part);
	template_text.append(CRLF);
	template_text.append(script_template.windows_part);
	return template_text;
}

std::string buildScriptTextImpl(
	FileType file_type,
	const std::string& first_filename,
	const UserArguments& user_args) {

	const ScriptTemplate script_template = getScriptTemplate(file_type);
	const std::string template_text = joinScriptTemplate(script_template);
	const std::vector<PlaceholderReplacement> replacements =
		makePlaceholderReplacements(template_text, first_filename, user_args);
	std::string script_text = renderTemplate(template_text, replacements);
	ensureNoUnresolvedPlaceholders(script_text);
	return script_text;
}

}  // namespace

namespace script_builder_internal {

std::string buildScriptText(
	FileType file_type,
	const std::string& first_filename,
	const UserArguments& user_args) {
	return buildScriptTextImpl(file_type, first_filename, user_args);
}

}  // namespace script_builder_internal
