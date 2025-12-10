#include <iostream>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <cstring>     // strdup
#include <unistd.h>    // fork(), execv(), access(), X_OK
#include <sys/wait.h>  // waitpid()
#include <fcntl.h>     // open() - 新增用于文件操作
#include <termios.h>   // termios for raw mode
#include <algorithm>   // sort
#include <set>         // set for unique sorted matches

#ifdef _WIN32
#include <io.h>
#define access _access
#define X_OK 4
#else
#include <unistd.h>
#endif

// Builtin commands for autocompletion
const std::vector<std::string> BUILTIN_COMMANDS = {"echo", "exit", "type"};

// Enable raw mode for terminal
struct termios orig_termios;

//ICANON (规范模式) - 默认情况下，终端会等用户按回车才把整行输入发给程序。关闭后，程序可以逐字符读取，包括 TAB 键
//ECHO - 默认终端会自动显示用户输入。关闭后，我们需要手动控制显示，这样才能在补全时正确更新显示内容

void disableRawMode()
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode()
{
	tcgetattr(STDIN_FILENO, &orig_termios);   // 保存当前终端设置
	struct termios raw = orig_termios;        // 复制一份用于修改
	raw.c_lflag &= ~(ICANON | ECHO);          // 关闭两个标志位
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); // 应用新设置
}

// Read a line with tab completion support
std::string readLineWithCompletion()
{
	enableRawMode();
	std::string input;
	int tabCount = 0;        // 跟踪连续按 Tab 的次数
	std::string lastInput;   // 记录上次按 Tab 时的输入
	
	while (true)
	{
		char c;
		if (read(STDIN_FILENO, &c, 1) != 1)//从标准输入读取 1 个字节到变量 c
		{
			disableRawMode();
			return input;
		}
		
		if (c == '\n' || c == '\r')
		{
			std::cout << std::endl;
			break;
		}
		else if (c == '\t')
		{
			// Tab completion
			// 检查输入是否改变，如果改变则重置 tabCount
			if (input != lastInput)
			{
				tabCount = 0;
				lastInput = input;
			}
			tabCount++;
			
			// Find matching builtin command or executable in PATH
			std::set<std::string> matches; // 使用 set 自动排序和去重
			
			// First check builtin commands
			for (const auto& cmd : BUILTIN_COMMANDS)
			{
				if (cmd.rfind(input, 0) == 0) // starts with input
				{
					matches.insert(cmd);
				}
			}
			
			// Then check executables in PATH
			char* pathEnv = std::getenv("PATH");
			if (pathEnv != nullptr)
			{
				std::string pathStr(pathEnv);
				size_t start = 0;
				while (true)
				{
					size_t end = pathStr.find(':', start);
					std::string dir = (end == std::string::npos)
						? pathStr.substr(start)
						: pathStr.substr(start, end - start);
					
					if (!dir.empty())
					{
						// Check if directory exists
						if (std::filesystem::exists(dir) && std::filesystem::is_directory(dir))
						{
							try
							{
								for (const auto& entry : std::filesystem::directory_iterator(dir))
								{
									if (entry.is_regular_file())
									{
										std::string filename = entry.path().filename().string();
										if (filename.rfind(input, 0) == 0) // starts with input
										{
											// Check if executable
											if (access(entry.path().c_str(), X_OK) == 0)
											{
												matches.insert(filename);
											}
										}
									}
								}
							}
							catch (...)
							{
								// Ignore errors when reading directory
							}
						}
					}
					
					if (end == std::string::npos) break;
					start = end + 1;
				}
			}
			
			if (matches.size() == 1)
			{
				// 唯一匹配：补全并添加空格
				std::string match = *matches.begin();
				for (size_t i = 0; i < input.length(); i++)
				{
					std::cout << "\b \b";
				}
				input = match + " ";
				std::cout << input;
				std::cout.flush();
				tabCount = 0; // 重置 tab 计数
			}
			else if (matches.size() > 1)
			{
				// 多个匹配
				if (tabCount == 1)
				{
					// 第一次按 Tab：响铃
					std::cout << '\x07';
					std::cout.flush();
				}
				else if (tabCount >= 2)
				{
					// 第二次按 Tab：显示所有匹配项
					std::cout << std::endl;
					bool first = true;
					for (const auto& m : matches)
					{
						if (!first)
						{
							std::cout << "  "; // 两个空格分隔
						}
						std::cout << m;
						first = false;
					}
					std::cout << std::endl;
					// 重新显示提示符和原始输入
					std::cout << "$ " << input;
					std::cout.flush();
					tabCount = 0; // 重置 tab 计数
				}
			}
			else
			{
				// 没有匹配：响铃
				std::cout << '\x07';
				std::cout.flush();
			}
		}
		else if (c == 127 || c == '\b')
		{
			// Backspace
			if (!input.empty())
			{
				input.pop_back();
				std::cout << "\b \b";
				std::cout.flush();
			}
		}
		else if (c >= 32)
		{
			// Regular character
			input += c;
			std::cout << c;
			std::cout.flush();
		}
	}
	
	disableRawMode();
	return input;
}

struct ArgToken
{
	std::string value;
	bool singleQuoted;
};

struct CommandInfo
{
	std::vector<ArgToken> args{};
	std::string outputFile;
	bool hasOutputRedirect{};
	std::string errorFile;
	bool hasErrorRedirect{};
	bool appendOutput{}; // 新增：是否为追加模式
	bool appendError{};  // 新增：错误输出是否为追加模式
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

CommandInfo parseCommand(const std::string& command)
{
	CommandInfo cmdInfo;
	cmdInfo.hasOutputRedirect = false;
	cmdInfo.hasErrorRedirect = false;
	cmdInfo.appendOutput = false; // 初始化追加标志
	cmdInfo.appendError = false;  // 初始化错误追加标志

	std::vector<ArgToken> args;
	std::string currentArg;
	bool inSingleQuotes = false;
	bool inDoubleQuotes = false;
	bool escapeNext = false;
	bool argSingleQuoted = false;
	bool foundRedirect = false;
	bool foundErrorRedirect = false;

	for (size_t i = 0; i < command.length(); ++i)
	{
		char c = command[i];

		if (escapeNext)
		{
			// shell 认为转义字符可以转义任何字符
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
					// 无引号时空格 tab ' " \ 可被转义
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
				// 单引号中转义无效
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

		// 检查重定向操作符（不在引号内）
		if (!inSingleQuotes && !inDoubleQuotes && !foundRedirect && !foundErrorRedirect)
		{
			// 检查2>>错误追加重定向语法
			if (c == '2' && i + 2 < command.length() && command[i + 1] == '>' && command[i + 2] == '>')
			{
				// 找到2>>追加重定向操作符
				if (!currentArg.empty())
				{
					args.push_back({ currentArg, argSingleQuoted });
					currentArg.clear();
					argSingleQuoted = false;
				}

				foundErrorRedirect = true;
				cmdInfo.hasErrorRedirect = true;
				cmdInfo.appendError = true; // 设置为追加模式

				// 跳过2和>>三个字符
				i += 2; // 跳过>>
				continue;
			}
			// 检查1>>输出追加重定向语法
			else if (c == '1' && i + 2 < command.length() && command[i + 1] == '>' && command[i + 2] == '>')
			{
				// 找到1>>追加重定向操作符
				if (!currentArg.empty())
				{
					args.push_back({ currentArg, argSingleQuoted });
					currentArg.clear();
					argSingleQuoted = false;
				}

				foundRedirect = true;
				cmdInfo.hasOutputRedirect = true;
				cmdInfo.appendOutput = true; // 设置为追加模式

				// 跳过1和>>三个字符
				i += 2; // 跳过>>
				continue;
			}
			// 检查>>输出追加重定向语法
			else if (c == '>' && i + 1 < command.length() && command[i + 1] == '>')
			{
				// 找到 >> 追加重定向操作符
				if (!currentArg.empty())
				{
					args.push_back({ currentArg, argSingleQuoted });
					currentArg.clear();
					argSingleQuoted = false;
				}

				foundRedirect = true;
				cmdInfo.hasOutputRedirect = true;
				cmdInfo.appendOutput = true; // 设置为追加模式

				// 跳过 >> 两个字符
				i++; // 跳过第二个>
				continue;
			}
			// 检查2>错误重定向语法
			else if (c == '2' && i + 1 < command.length() && command[i + 1] == '>')
			{
				// 找到2>重定向操作符
				if (!currentArg.empty())
				{
					args.push_back({ currentArg, argSingleQuoted });
					currentArg.clear();
					argSingleQuoted = false;
				}

				foundErrorRedirect = true;
				cmdInfo.hasErrorRedirect = true;
				cmdInfo.appendError = false; // 设置为覆盖模式

				// 跳过2和>两个字符
				i++; // 跳过>
				continue;
			}
			// 检查1>重定向语法
			else if (c == '1' && i + 1 < command.length() && command[i + 1] == '>')
			{
				// 找到1>重定向操作符
				if (!currentArg.empty())
				{
					args.push_back({ currentArg, argSingleQuoted });
					currentArg.clear();
					argSingleQuoted = false;
				}

				foundRedirect = true;
				cmdInfo.hasOutputRedirect = true;
				cmdInfo.appendOutput = false; // 设置为覆盖模式

				// 跳过1和>两个字符
				i++; // 跳过>
				continue;
			}
			else if (c == '>' && (i + 1 < command.length() && command[i + 1] != '>'))
			{
				// 找到 > 重定向操作符
				if (!currentArg.empty())
				{
					args.push_back({ currentArg, argSingleQuoted });
					currentArg.clear();
					argSingleQuoted = false;
				}

				foundRedirect = true;
				cmdInfo.hasOutputRedirect = true;
				cmdInfo.appendOutput = false; // 设置为覆盖模式

				// 跳过 > 字符
				continue;
			}
		}

		if (!inSingleQuotes && !inDoubleQuotes && (c == ' ' || c == '\t'))
		{
			if (!currentArg.empty())
			{
				if (foundRedirect || foundErrorRedirect)
				{
					// 重定向操作符后的空格分隔文件名
					if (foundRedirect && !cmdInfo.outputFile.empty())
					{
						// 文件名中不应有空格，如果已经有文件名则忽略后续内容
						break;
					}
					if (foundErrorRedirect && !cmdInfo.errorFile.empty())
					{
						// 文件名中不应有空格，如果已经有文件名则忽略后续内容
						break;
					}
					if (foundRedirect)
					{
						cmdInfo.outputFile = currentArg;
						currentArg.clear();
					}
					else if (foundErrorRedirect)
					{
						cmdInfo.errorFile = currentArg;
						currentArg.clear();
					}
				}
				else
				{
					args.push_back({ currentArg, argSingleQuoted });
					currentArg.clear();
					argSingleQuoted = false;
				}
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
		if (foundRedirect)
		{
			cmdInfo.outputFile = currentArg;
		}
		else if (foundErrorRedirect)
		{
			cmdInfo.errorFile = currentArg;
		}
		else
		{
			args.push_back({ currentArg, argSingleQuoted });
		}
	}

	cmdInfo.args = args;
	return cmdInfo;
}

int main()
{
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;

	while (true)
	{
		std::cout << "$ ";
		std::string command = readLineWithCompletion();

		if (command == "exit" || command == "exit ") break;

		CommandInfo cmdInfo = parseCommand(command);

		if (cmdInfo.args.empty())
		{
			continue;
		}

		// 处理echo命令
		if (cmdInfo.args[0].value == "echo")
		{
			// 如果有重定向，打开输出文件
			int outputFd = STDOUT_FILENO;
			bool shouldCloseFile = false;

			if (cmdInfo.hasOutputRedirect && !cmdInfo.outputFile.empty())
			{
				int flags = O_WRONLY | O_CREAT;
				if (cmdInfo.appendOutput)
				{
					flags |= O_APPEND; // 追加模式
				}
				else
				{
					flags |= O_TRUNC; // 覆盖模式
				}
				
				outputFd = open(cmdInfo.outputFile.c_str(), flags, 0644);
				if (outputFd == -1)
				{
					std::cerr << "Error: cannot open file " << cmdInfo.outputFile << std::endl;
					continue;
				}
				shouldCloseFile = true;
			}

			// 处理错误重定向：即使echo不产生stderr，也要创建文件
			if (cmdInfo.hasErrorRedirect && !cmdInfo.errorFile.empty())
			{
				int flags = O_WRONLY | O_CREAT;
				if (cmdInfo.appendError)
				{
					flags |= O_APPEND; // 追加模式
				}
				else
				{
					flags |= O_TRUNC; // 覆盖模式
				}
				
				int errorFd = open(cmdInfo.errorFile.c_str(), flags, 0644);
				if (errorFd == -1)
				{
					std::cerr << "Error: cannot open file " << cmdInfo.errorFile << std::endl;
					if (shouldCloseFile) close(outputFd);
					continue;
				}
				close(errorFd); // 立即关闭，因为echo不产生stderr
			}

			for (size_t i = 1; i < cmdInfo.args.size(); ++i)
			{
				if (i > 1)
				{
					write(outputFd, " ", 1);
				}

				std::string outputText;
				if (cmdInfo.args[i].singleQuoted)
				{
					outputText = cmdInfo.args[i].value;
				}
				else
				{
					outputText = decodeEchoEscapes(cmdInfo.args[i].value);
				}

				write(outputFd, outputText.c_str(), outputText.length());
			}

			write(outputFd, "\n", 1);

			if (shouldCloseFile)
			{
				close(outputFd);
			}

			continue;
		}

		// 处理type命令
		if (cmdInfo.args[0].value == "type")
		{
			if (cmdInfo.args.size() < 2)
			{
				std::cout << "type: missing argument" << std::endl;
				continue;
			}

			std::string target = cmdInfo.args[1].value;

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

		// 处理外部命令
		std::vector<char*> args;
		for (const auto& arg : cmdInfo.args)
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
							// 子进程：处理重定向
							if (cmdInfo.hasOutputRedirect && !cmdInfo.outputFile.empty())
							{
								int flags = O_WRONLY | O_CREAT;
								if (cmdInfo.appendOutput)
								{
									flags |= O_APPEND; // 追加模式
								}
								else
								{
									flags |= O_TRUNC; // 覆盖模式
								}
								
								int fd = open(cmdInfo.outputFile.c_str(), flags, 0644);
								if (fd == -1)
								{
									exit(1);
								}
								dup2(fd, STDOUT_FILENO);
								close(fd);
							}

							if (cmdInfo.hasErrorRedirect && !cmdInfo.errorFile.empty())
							{
								int flags = O_WRONLY | O_CREAT;
								if (cmdInfo.appendError)
								{
									flags |= O_APPEND; // 追加模式
								}
								else
								{
									flags |= O_TRUNC; // 覆盖模式
								}
								
								int fd = open(cmdInfo.errorFile.c_str(), flags, 0644);
								if (fd == -1)
								{
									exit(1);
								}
								dup2(fd, STDERR_FILENO);
								close(fd);
							}

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
			if (ptr)
			{
				free(ptr);
			}
		}
	}

	return 0;
}
