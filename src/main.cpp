#include <iostream>
#include <string>

int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  while (true)
  {
	  std::cout << "$ ";

	  std::string command;
	  std::getline(std::cin, command);

	  if (command == "exit")
	  {
		  break;
	  }
	  else if (command.substr(0, 4) == "echo")
	  {
		  std::cout << command.substr(5) << std::endl;
	  }
	  else if (command.substr(0, 5) == "type ")
	  {
		  std::string target = command.substr(5);

		  if (target == "echo" || target == "exit" || target == "type") 
		  {
			  std::cout << target << " is a shell builtin" << std::endl;
		  }
		  else
		  {
			  std::cout << target << ": not found" << std::endl;
		  }
	  }
	  else
	  {
		  std::cout << command << ": command not found" << std::endl;
	  }
  }
}
