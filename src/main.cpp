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

// Parse command with single, double quote and backslash escape support
std::vector<std::string> parseCommand(const std::string& command) 
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
            // Handle escape sequences in double quotes
            if (inDoubleQuotes) 
			{
                switch (c) 
				{
                    case 'n':
                        currentArg += '\n';
                        break;
                    case 't':
                        currentArg += '\t';
                        break;
                    case 'r':
                        currentArg += '\r';
                        break;
                    case '\\':
                        currentArg += '\\';
                        break;
                    case '"':
                        currentArg += '"';
                        break;
                    case '\'':
                        currentArg += '\'';
                        break;
                    default:
                        // For octal sequences (e.g., \67)
                        if (c >= '0' && c <= '7')
						{
                            // Parse up to 3 octal digits
                            int octalValue = c - '0';
                            size_t j = i + 1;
                            int digitCount = 1;
                            
                            while (j < command.length() && digitCount < 3) 
							{
                                char nextChar = command[j];
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
                            
                            currentArg += static_cast<char>(octalValue);
                            i = j - 1; // Adjust index to skip consumed digits
                        } 
						else 
						{
                            // Unknown escape, treat literally
                            currentArg += c;
                        }
                        break;
                }
            } 
			else
			{
                // Outside quotes, add escaped character literally
                currentArg += c;
            }
            escapeNext = false;
        }
        else if (c == '\\' && !inSingleQuotes && !inDoubleQuotes)
		{
            // Backslash outside quotes - escape next character
            escapeNext = true;
        }
        else if (c == '\\' && !inSingleQuotes && inDoubleQuotes) 
		{
            // Backslash inside double quotes - escape next character
            escapeNext = true;
        }
        else if (c == '\'' && !inDoubleQuotes && !escapeNext) 
		{
            inSingleQuotes = !inSingleQuotes;
        } 
		else if (c == '"' && !inSingleQuotes && !escapeNext)
		{
            inDoubleQuotes = !inDoubleQuotes;
        }
		else if ((c == ' ' || c == '\t') && !inSingleQuotes && !inDoubleQuotes && !escapeNext)
		{
            if (!currentArg.empty()) 
			{
                args.push_back(currentArg);
                currentArg.clear();
            }
        } 
		else
		{
            currentArg += c;
        }
    }
    
    // Handle pending escape at end of command
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

					std::cout << args[i];
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
