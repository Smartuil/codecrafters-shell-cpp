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

int main() {
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
			std::cout << command.substr(5) << std::endl;
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
		std::istringstream iss(command);
		std::vector<char*> args;
		std::string token;

		while (iss >> token) 
		{
			args.push_back(strdup(token.c_str()));
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
