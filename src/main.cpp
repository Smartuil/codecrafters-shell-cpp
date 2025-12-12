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
const std::vector<std::string> BUILTIN_COMMANDS = {"echo", "exit", "type", "history"};

// 命令历史记录
std::vector<std::string> commandHistory;

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

// 计算多个字符串的最长公共前缀
std::string longestCommonPrefix(const std::set<std::string>& strings)
{
	if (strings.empty()) return "";
	if (strings.size() == 1) return *strings.begin();
	
	// 取第一个字符串作为参考
	const std::string& first = *strings.begin();
	size_t prefixLen = first.length();
	
	for (const auto& s : strings)
	{
		size_t i = 0;
		while (i < prefixLen && i < s.length() && first[i] == s[i])
		{
			i++;
		}
		prefixLen = i;
	}
	
	return first.substr(0, prefixLen);
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
				lastInput = input;
			}
			else if (matches.size() > 1)
			{
				// 多个匹配：计算最长公共前缀
				std::string lcp = longestCommonPrefix(matches);
				
				if (lcp.length() > input.length())
				{
					// 可以补全到更长的公共前缀
					for (size_t i = 0; i < input.length(); i++)
					{
						std::cout << "\b \b";
					}
					input = lcp;
					std::cout << input;
					std::cout.flush();
					tabCount = 0; // 重置 tab 计数
					lastInput = input;
				}
				else
				{
					// 无法进一步补全
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

// 按管道符分割命令行
std::vector<std::string> splitByPipe(const std::string& command)
{
	std::vector<std::string> commands;
	std::string current;
	bool inSingleQuotes = false;
	bool inDoubleQuotes = false;
	bool escapeNext = false;

	for (size_t i = 0; i < command.length(); ++i)
	{
		char c = command[i];

		if (escapeNext)
		{
			current += c;
			escapeNext = false;
			continue;
		}

		if (c == '\\' && !inSingleQuotes)
		{
			current += c;
			escapeNext = true;
			continue;
		}

		if (c == '\'' && !inDoubleQuotes)
		{
			inSingleQuotes = !inSingleQuotes;
			current += c;
			continue;
		}

		if (c == '"' && !inSingleQuotes)
		{
			inDoubleQuotes = !inDoubleQuotes;
			current += c;
			continue;
		}

		if (c == '|' && !inSingleQuotes && !inDoubleQuotes)
		{
			// 去除前后空格
			size_t start = current.find_first_not_of(" \t");
			size_t end = current.find_last_not_of(" \t");
			if (start != std::string::npos)
			{
				commands.push_back(current.substr(start, end - start + 1));
			}
			current.clear();
			continue;
		}

		current += c;
	}

	// 添加最后一个命令
	size_t start = current.find_first_not_of(" \t");
	size_t end = current.find_last_not_of(" \t");
	if (start != std::string::npos)
	{
		commands.push_back(current.substr(start, end - start + 1));
	}

	return commands;
}

// 在PATH中查找可执行文件
std::string findExecutable(const std::string& cmd)
{
	char* pathEnv = std::getenv("PATH");
	if (pathEnv == nullptr) return "";

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
				return fullPath;
			}
		}

		if (end == std::string::npos) break;
		start = end + 1;
	}

	return "";
}

// 检查是否是内置命令
bool isBuiltinCommand(const std::string& cmd)
{
	return cmd == "echo" || cmd == "exit" || cmd == "type" || cmd == "cd" || cmd == "pwd" || cmd == "history";
}

// 在子进程中执行内置命令（用于管道）
void executeBuiltinInPipeline(const CommandInfo& cmdInfo)
{
	const std::string& cmd = cmdInfo.args[0].value;

	if (cmd == "echo")
	{
		for (size_t i = 1; i < cmdInfo.args.size(); ++i)
		{
			if (i > 1)
			{
				std::cout << " ";
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
			std::cout << outputText;
		}
		std::cout << std::endl;
	}
	else if (cmd == "type")
	{
		if (cmdInfo.args.size() < 2)
		{
			std::cout << "type: missing argument" << std::endl;
			return;
		}

		std::string target = cmdInfo.args[1].value;

		if (target == "echo" || target == "exit" || target == "type" || target == "cd" || target == "pwd")
		{
			std::cout << target << " is a shell builtin" << std::endl;
			return;
		}

		std::string execPath = findExecutable(target);
		if (!execPath.empty())
		{
			std::cout << target << " is " << execPath << std::endl;
		}
		else
		{
			std::cout << target << ": not found" << std::endl;
		}
	}
	else if (cmd == "pwd")
	{
		char cwd[4096];
		if (getcwd(cwd, sizeof(cwd)) != nullptr)
		{
			std::cout << cwd << std::endl;
		}
	}
	else if (cmd == "history")
	{
		size_t start = 0;
		size_t count = commandHistory.size();
		
		// 检查是否有参数限制显示数量
		if (cmdInfo.args.size() >= 2)
		{
			try
			{
				int n = std::stoi(cmdInfo.args[1].value);
				if (n > 0 && static_cast<size_t>(n) < commandHistory.size())
				{
					start = commandHistory.size() - n;
				}
			}
			catch (...)
			{
				// 无效参数，显示全部
			}
		}
		
		for (size_t i = start; i < count; ++i)
		{
			std::cout << "    " << (i + 1) << "  " << commandHistory[i] << std::endl;
		}
	}
	// exit 和 cd 在管道中不太有意义，但可以简单处理
}

// 执行管道命令
void executePipeline(const std::vector<std::string>& pipeCommands)
{
	int numCmds = pipeCommands.size();
	std::vector<int> pipeFds((numCmds - 1) * 2);

	// 创建所有管道
	for (int i = 0; i < numCmds - 1; ++i)
	{
		if (pipe(&pipeFds[i * 2]) == -1)
		{
			std::cerr << "pipe failed" << std::endl;
			return;
		}
	}

	std::vector<pid_t> pids;

	for (int i = 0; i < numCmds; ++i)
	{
		CommandInfo cmdInfo = parseCommand(pipeCommands[i]);
		if (cmdInfo.args.empty()) continue;

		std::string cmdName = cmdInfo.args[0].value;
		bool isBuiltin = isBuiltinCommand(cmdName);
		std::string execPath;
		
		if (!isBuiltin)
		{
			execPath = findExecutable(cmdName);
			if (execPath.empty())
			{
				std::cerr << cmdName << ": command not found" << std::endl;
				continue;
			}
		}

		pid_t pid = fork();
		if (pid == 0)
		{
			// 管道执行流程详解：

			// 假设执行 "cmd0 | cmd1 | cmd2"（3个命令，2个管道）

			// 管道数组结构：
			// pipeFds[0] = 管道0读端    pipeFds[1] = 管道0写端
			// pipeFds[2] = 管道1读端    pipeFds[3] = 管道1写端

			// 数据流向：
			// cmd0 ---> 管道0 ---> cmd1 ---> 管道1 ---> cmd2
			// 	写端[1]    读端[0]  写端[3]    读端[2]

			// 各命令的重定向：
			// cmd0 (i=0): stdin=终端,        stdout=pipeFds[1] (管道0写端)
			// cmd1 (i=1): stdin=pipeFds[0],  stdout=pipeFds[3] (管道1写端)
			// cmd2 (i=2): stdin=pipeFds[2],  stdout=终端

			// 索引计算公式：
			// 读取前一个管道: pipeFds[(i-1)*2]   -> 读端
			// 写入当前管道:   pipeFds[i*2+1]     -> 写端

			// fork() 返回值：
			// 子进程中返回 0
			// 父进程中返回子进程PID
			// 失败返回 -1

			// dup2(oldFd, newFd) 作用：
			// 将 newFd 重定向到 oldFd，之后对 newFd 的操作实际作用于 oldFd

			// 为什么要关闭所有管道文件描述符：
			// 1. dup2 已复制了需要的描述符
			// 2. 不关闭写端会导致读端无法检测 EOF
			// 3. 避免文件描述符泄漏

			// execv 说明：
			// 用新程序替换当前进程，成功则不返回
			// 参数数组必须以 nullptr 结尾

			// 图解示例： cat file | wc
			// 命令0: cat file          命令1: wc
			// 	i=0                     i=1

			// [stdin]                 pipeFds[0] ──→ [stdin]
			// 	↓                         ↑              ↓
			// cat                      管道0            wc
			// 	↓                         ↑              ↓
			// [stdout] ──→ pipeFds[1] ────┘          [stdout]
			// cat (i=0)：

			// 不重定向 stdin（i=0，跳过）
			// stdout → pipeFds[1]（管道0写端）
			// wc (i=1)：

			// stdin ← pipeFds[0]（管道0读端）
			// 不重定向 stdout（i=1 是最后一个，跳过）

			// 子进程

			// 如果不是第一个命令，从前一个管道读取
			if (i > 0)
			{
				dup2(pipeFds[(i - 1) * 2], STDIN_FILENO);
			}

			// 如果不是最后一个命令，写入到下一个管道
			if (i < numCmds - 1)
			{
				dup2(pipeFds[i * 2 + 1], STDOUT_FILENO);
			}

			// 关闭所有管道文件描述符
			for (size_t j = 0; j < pipeFds.size(); ++j)
			{
				close(pipeFds[j]);
			}

			if (isBuiltin)
			{
				// 执行内置命令
				executeBuiltinInPipeline(cmdInfo);
				exit(0);
			}
			else
			{
				// 构建参数数组
				std::vector<char*> args;
				for (const auto& arg : cmdInfo.args)
				{
					args.push_back(strdup(arg.value.c_str()));
				}
				args.push_back(nullptr);

				execv(execPath.c_str(), args.data());
				exit(1); // execv 失败才会执行到这里
			}
		}
		else if (pid > 0)
		{
			pids.push_back(pid);
		}
	}

	// 父进程关闭所有管道
	for (size_t i = 0; i < pipeFds.size(); ++i)
	{
		close(pipeFds[i]);
	}

	// 等待所有子进程
	for (pid_t pid : pids)
	{
		waitpid(pid, nullptr, 0);
	}
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

		// 添加到历史记录（去除尾部空格）
		std::string trimmedCmd = command;
		while (!trimmedCmd.empty() && (trimmedCmd.back() == ' ' || trimmedCmd.back() == '\t'))
		{
			trimmedCmd.pop_back();
		}
		if (!trimmedCmd.empty())
		{
			commandHistory.push_back(trimmedCmd);
		}

		// 检查是否包含管道
		std::vector<std::string> pipeCommands = splitByPipe(command);
		if (pipeCommands.size() > 1)
		{
			// 执行管道命令
			executePipeline(pipeCommands);
			continue;
		}

		CommandInfo cmdInfo = parseCommand(command);

		if (cmdInfo.args.empty())
		{
			continue;
		}

		// 处理history命令
		if (cmdInfo.args[0].value == "history")
		{
			size_t start = 0;
			size_t count = commandHistory.size();
			
			// 检查是否有参数限制显示数量
			if (cmdInfo.args.size() >= 2)
			{
				try
				{
					int n = std::stoi(cmdInfo.args[1].value);
					if (n > 0 && static_cast<size_t>(n) < commandHistory.size())
					{
						start = commandHistory.size() - n;
					}
				}
				catch (...)
				{
					// 无效参数，显示全部
				}
			}
			
			for (size_t i = start; i < count; ++i)
			{
				std::cout << "    " << (i + 1) << "  " << commandHistory[i] << std::endl;
			}
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

			if (target == "echo" || target == "exit" || target == "type" || target == "history")
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
