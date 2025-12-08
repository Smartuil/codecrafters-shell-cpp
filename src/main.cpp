#include <iostream>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <cstring>     // ADD THIS ✔
#include <unistd.h>    // fork(), execv(), access(), X_OK
#include <sys/wait.h>  // waitpid()

// Cross-platform compatibility for access() and X_OK
#ifdef _WIN32
#include <io.h>
#define access _access
#define X_OK 4
#else
#include <unistd.h>
#endif

std::string decodeEchoEscapes(const std::string& input)
{
	std::string result;
	bool escapeNext = false;

	for (size_t i = 0; i < input.length(); ++i)
	{
		char c = input[i];

		if (escapeNext)
		{
			switch (c)
			{
			case 'n':
				result += '\n';
				break;

			case 't':
				result += '\t';
				break;

			case 'r':
				result += '\r';
				break;

			case '\\':
				result += '\\';
				break;

			case '"':
				result += '"';
				break;

			default:
				// 八进制转义：\123
				if (c >= '0' && c <= '7')
				{
					int octalValue = c - '0';
					size_t j = i + 1;
					int digitCount = 1;

					while (j < input.length() && digitCount < 3)
					{
						char nextChar = input[j];
						if (nextChar >= '0' && nextChar <= '7')
						{
							octalValue = octalValue * 8 + (nextChar - '0');
							j++;
							digitCount++;
						}
						else
						{
							break;
						}
					}

					result += static_cast<char>(octalValue);
					i = j - 1;
				}
				else
				{
					// 未知转义：原样输出
					result += '\\';
					result += c;
				}

				break;
			}

			escapeNext = false;
		}
		else if (c == '\\')
		{
			escapeNext = true;
		}
		else
		{
			result += c;
		}
	}

	// 结尾有反斜杠：按字面输出 '\'
	if (escapeNext)
	{
		result += '\\';
	}

	return result;
}

std::vector<std::string> parseCommand(const std::string & command)
{
	std::vector<std::string> args;
	std::string currentArg;
	bool inSingleQuotes = false;
	bool inDoubleQuotes = false;
	bool escapeNext = false;

	for (size_t i = 0; i < command.length(); ++i)
	{
		char c = command[i];

		if (escapeNext)
		{
			// shell 行为：单引号内无任何转义
			if (!inSingleQuotes)
			{
				if (inDoubleQuotes)
				{
					// 双引号中仅 \" \\ \$ \` 有效
					if (c == '"' || c == '\\' || c == '$' || c == '`')
					{
						currentArg += c;
					}
					else
					{
						currentArg += '\\';
						currentArg += c;
					}
				}
				else
				{
					// 引号外仅空格 tab ' " \ 可被转义
					if (c == ' ' || c == '\t' || c == '\'' || c == '"' || c == '\\')
					{
						currentArg += c;
					}
					else
					{
						currentArg += '\\';
						currentArg += c;
					}
				}
			}
			else
			{
				// 单引号内转义无效
				currentArg += '\\';
				currentArg += c;
			}

			escapeNext = false;
			continue;
		}

		// 反斜杠触发转义（单引号内无效）
		if (c == '\\' && !inSingleQuotes)
		{
			escapeNext = true;
			continue;
		}

		// 单引号切换（仅在不在双引号时）
		if (c == '\'' && !inDoubleQuotes)
		{
			inSingleQuotes = !inSingleQuotes;
			continue;
		}

		// 双引号切换（仅在不在单引号时）
		if (c == '"' && !inSingleQuotes)
		{
			inDoubleQuotes = !inDoubleQuotes;
			continue;
		}

		// 空白分隔（仅在非引号状态）
		if (!inSingleQuotes && !inDoubleQuotes && (c == ' ' || c == '\t'))
		{
			if (!currentArg.empty())
			{
				args.push_back(currentArg);
				currentArg.clear();
			}
			continue;
		}

		currentArg += c;
	}

	// 结尾如果有 pending escape → literal '\'
	if (escapeNext)
	{
		currentArg += '\\';
	}

	if (!currentArg.empty())
	{
		args.push_back(currentArg);
	}

	return args;
}

int main() 
{
	// Flush after every std::cout / std:cerr
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;

	std::string pathString = getenv("PATH");

	std::string currentPath;
	std::string fullPath;
	char sep = std::filesystem::path::preferred_separator;
	bool foundPath = false;

	while (true)
	{
		std::cout << "$ ";

		std::string command;
		std::getline(std::cin, command);

		if (command == "exit")
		{
			break;
		}

		if (command.substr(0, 4) == "echo")
		{
			std::vector<std::string> args = parseCommand(command);
			if (args.size() > 1)
			{
				for (size_t i = 1; i < args.size(); ++i)
				{
					if (i > 1)
					{
						std::cout << " ";
					}

					std::cout << decodeEchoEscapes(args[i]);
				}
			}

			std::cout << std::endl;

			continue;
		}

		if (command.substr(0, 5) == "type ")
		{
			std::string target = command.substr(5);

			if (target == "echo" || target == "exit" || target == "type")
			{
				std::cout << target << " is a shell builtin" << std::endl;

				continue;
			}
			
			// Search in PATH
			char* pathEnv = std::getenv("PATH");

			if (pathEnv != nullptr) 
			{
				std::string path(pathEnv);
				size_t start = 0;
				bool found = false;

				while (true) 
				{
					size_t end = path.find(
	#ifdef _WIN32
						';'
	#else
						':'
	#endif
						, start);

					std::string dir = (end == std::string::npos)
						? path.substr(start)
						: path.substr(start, end - start);

					if (!dir.empty())
					{
						// Build full path
						std::string fullPath =
	#ifdef _WIN32
							dir + "\\" + target;
	#else
							dir + "/" + target;
	#endif

						// Check executable permission
						if (access(fullPath.c_str(), X_OK) == 0) 
						{
							std::cout << target << " is " << fullPath << std::endl;
							found = true;
							break;
						}
					}

					if (end == std::string::npos) 
					{
						break;
					}

					start = end + 1;
				}

				if (!found) 
				{
					std::cout << target << ": not found" << std::endl;
				}
			}
			else 
			{
				std::cout << target << ": not found" << std::endl;

			}

			continue;
		}

		// ---------- external command execution ----------
		std::vector<std::string> parsedArgs = parseCommand(command);
		if (parsedArgs.empty()) continue;
		
		std::vector<char*> args;
		for (const auto& arg : parsedArgs)
		{
			args.push_back(strdup(arg.c_str()));
		}
		args.push_back(nullptr);

		char* cmd = args[0];
		char* pathEnv = std::getenv("PATH");
		bool executed = false;

		if (pathEnv != nullptr)
		{
			std::string path(pathEnv);
			size_t start = 0;

			while (true) 
			{
				size_t end = path.find(':', start);
				std::string dir = (end == std::string::npos)
					? path.substr(start)
					: path.substr(start, end - start);

				if (!dir.empty()) 
				{
					std::string fullPath = dir + "/" + cmd;

					if (access(fullPath.c_str(), X_OK) == 0) 
					{
						pid_t pid = fork();

						if (pid == 0) 
						{ 
							// child
							execv(fullPath.c_str(), args.data());
							exit(1);
						}
						else 
						{ 
							// parent
							waitpid(pid, nullptr, 0);
						}

						executed = true;

						break;
					}
				}

				if (end == std::string::npos)
				{
					break;
				}

				start = end + 1;
			}
		}

		if (!executed) 
		{
			std::cout << cmd << ": command not found" << std::endl;
		}

		for (char* ptr : args) 
		{
			if (ptr)
			{ 
				free(ptr);
			}
		}
	}

	return 0;
}
