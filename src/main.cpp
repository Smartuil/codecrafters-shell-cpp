#include <iostream>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <cstring>     // strdup
#include <unistd.h>    // fork(), execv(), access(), X_OK
#include <sys/wait.h>  // waitpid()

#ifdef _WIN32
#include <io.h>
#define access _access
#define X_OK 4
#else
#include <unistd.h>
#endif

struct ArgToken
{
	std::string value;
	bool singleQuoted;
};

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
			case 'n': result += 'n'; break;
			case 't': result += 't'; break;
			case 'r': result += 'r'; break;
			case '\\': result += '\\'; break;
			case '"': result += '"'; break;
			default:
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
						else break;
					}

					result += static_cast<char>(octalValue);
					i = j - 1;
				}
				else
				{
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

	if (escapeNext)
	{
		result += '\\';
	}

	return result;
}

std::vector<ArgToken> parseCommand(const std::string& command)
{
	std::vector<ArgToken> args;
	std::string currentArg;
	bool inSingleQuotes = false;
	bool inDoubleQuotes = false;
	bool escapeNext = false;
	bool argSingleQuoted = false;

	for (size_t i = 0; i < command.length(); ++i)
	{
		char c = command[i];

		if (escapeNext)
		{
			if (!inSingleQuotes)
			{
				currentArg += c;
			}
			else
			{
				currentArg += '\\';
				currentArg += c;
			}

			escapeNext = false;
			continue;
		}

		if (c == '\\' && !inSingleQuotes)
		{
			escapeNext = true;
			continue;
		}

		if (c == '\'' && !inDoubleQuotes)
		{
			inSingleQuotes = !inSingleQuotes;
			if (inSingleQuotes) argSingleQuoted = true;
			continue;
		}

		if (c == '"' && !inSingleQuotes)
		{
			inDoubleQuotes = !inDoubleQuotes;
			continue;
		}

		if (!inSingleQuotes && !inDoubleQuotes && (c == ' ' || c == '\t'))
		{
			if (!currentArg.empty())
			{
				args.push_back({ currentArg, argSingleQuoted });
				currentArg.clear();
				argSingleQuoted = false;
			}
			continue;
		}

		currentArg += c;
	}

	if (escapeNext)
	{
		currentArg += '\\';
	}

	if (!currentArg.empty())
	{
		args.push_back({ currentArg, argSingleQuoted });
	}

	return args;
}

int main()
{
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;

	while (true)
	{
		std::cout << "$ ";
		std::string command;
		std::getline(std::cin, command);

		if (command == "exit") break;

		if (command.substr(0, 4) == "echo")
		{
			std::vector<ArgToken> args = parseCommand(command);
			for (size_t i = 1; i < args.size(); ++i)
			{
				if (i > 1)
				{
					std::cout << " ";
				}

				if (args[i].singleQuoted)
				{
					std::cout << args[i].value;
				}
				else
				{
					std::cout << decodeEchoEscapes(args[i].value);
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

			char* pathEnv = std::getenv("PATH");
			bool found = false;
			if (pathEnv != nullptr)
			{
				std::string path(pathEnv);
				size_t start = 0;
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
						std::string fullPath =
#ifdef _WIN32
							dir + "\\" + target;
#else
							dir + "/" + target;
#endif
						if (access(fullPath.c_str(), X_OK) == 0)
						{
							std::cout << target << " is " << fullPath << std::endl;
							found = true;
							break;
						}
					}

					if (end == std::string::npos) break;
					start = end + 1;
				}
			}
			if (!found)
			{
				std::cout << target << ": not found" << std::endl;
			}

			continue;
		}

		std::vector<ArgToken> parsedArgs = parseCommand(command);
		if (parsedArgs.empty())
		{
			continue;
		}

		std::vector<char*> args;
		for (const auto& arg : parsedArgs)
		{
			args.push_back(strdup(arg.value.c_str()));
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
							execv(fullPath.c_str(), args.data());
							exit(1);
						}
						else
						{
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
			if
			{ 
				(ptr)
			}
			
			free(ptr);
		}
	}

	return 0;
}
