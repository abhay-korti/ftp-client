#include <iostream>
#include <array>
#include <string>
#include <netdb.h>
#include <unistd.h>
#include <string.h>

#define BUFFER_SIZE 4096

int sendMessageStatus(int sockfd) {
  char buf[BUFFER_SIZE];
  memset(buf, 0, BUFFER_SIZE);
  ssize_t bufferBytesUsed = 0;

  while (1) {
    char* pointer = strstr(buf, "\r\n");
    if (pointer) break;
    if (bufferBytesUsed == sizeof(buf)) {
      std::cerr << "Recv Error: Recv Buffer Full\n";
      close(sockfd);
      return -1;
    }
    ssize_t n = recv(sockfd, buf + bufferBytesUsed,
                     sizeof(buf) - bufferBytesUsed - 1, 0);
    if (n < 0) {
      std::cerr << "Recv Error: Error Receiving Data\n";
      close(sockfd);
      return -1;
    }
    if (n == 0) break;
    bufferBytesUsed += n;
  }

  buf[bufferBytesUsed] = '\0';

  std::cout << "Bytes Recevied:\n" << buf << "\n";

  if (buf[0] == '2')
    return 1;
  else
    return 0;
}

void setupConnection(int* sockfd, std::array<std::string, 7>& argArray) {
  struct addrinfo hints, *p, *start;
  int status;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = AF_UNSPEC;
  if ((status = getaddrinfo(argArray[3].c_str(), argArray[4].c_str(), &hints,
                            &start)) != 0) {
    std::cerr << "Encountered an error trying to establish connection\n";
    exit(1);
  }

  p = start;
  while (p != NULL) {
    *sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (*sockfd == -1) continue;
    if (connect(*sockfd, p->ai_addr, p->ai_addrlen) == 0) break;
    close(*sockfd);
    p = p->ai_next;
  }

  if (p == NULL) {
    std::cerr << "None of the attempts made resulted in a valid connection\n";
    freeaddrinfo(start);
    exit(1);
  }
  freeaddrinfo(start);
}

void initMessage(int sockfd, std::array<std::string, 7>& argArray) {
  // Receive Hello message first
  std::string receviedMessage = "";
  sendMessageStatus(sockfd);

  // All bytes received
  // Login, Password, setup type - and mode if the command is
  // related to uploading/ downloading content

  char userMessage[BUFFER_SIZE];
  sprintf(userMessage, "USER %s\r\n", argArray[1].c_str());
  std::cout << "Sending Message: \n" << userMessage << "\n";
  if (send(sockfd, userMessage, strlen(userMessage), 0) < 0) {
    std::cerr << "Init Error: Error sending USER message to server\n";
    close(sockfd);
    exit(1);
  }

  sendMessageStatus(sockfd);

  char passwordMessage[BUFFER_SIZE];
  sprintf(passwordMessage, "PASS %s\r\n", argArray[2].c_str());
  std::cout << "Sending Message: \n" << passwordMessage << "\n";
  if (send(sockfd, passwordMessage, strlen(passwordMessage), 0) < 0) {
    std::cerr << "Init Error: Error sending Password message to server\n";
    close(sockfd);
    exit(1);
  }

  sendMessageStatus(sockfd);

  const char* makeDirectoryMessage = "MKD ./new-dir\r\n";
  std::cout << "Sending Message: \n" << makeDirectoryMessage << "\n";
  if (send(sockfd, makeDirectoryMessage, strlen(makeDirectoryMessage), 0) < 0) {
    std::cerr << "Init Error: Error sending LIST message to server\n";
    close(sockfd);
    exit(1);
  }

  sendMessageStatus(sockfd);

  const char* deleteDirectoryMessage = "RMD ./new-dir\r\n";
  std::cout << "Sending Message: \n" << deleteDirectoryMessage << "\n";
  if (send(sockfd, deleteDirectoryMessage, strlen(deleteDirectoryMessage), 0) <
      0) {
    std::cerr << "Init Error: Error sending RMD message to server\n";
    close(sockfd);
    exit(1);
  }

  sendMessageStatus(sockfd);
}

void parseInputParameters(int argc, char** argv,
                          std::array<std::string, 7>& argArray) {
  // ftp://ftp.example.com/ - USER = anonymous PORT: 21
  // ftp://bob:s3cr3t@ftp.example.com/ - port 21

  // Break the string up, cut it at the @
  // If that string is empty, User and Password are empty

  // Default arguments
  // Command
  argArray[0] = argv[1];
  // Username
  argArray[1] = "anonymous";
  // Password
  argArray[2] = "";
  // Host Name
  argArray[3] = "";
  // Port Number
  argArray[4] = "21";
  // First Arg
  argArray[5] = "/";
  // Second Arg
  argArray[6] = "";

  if (strstr(argv[2], "ftp://") == NULL) {
    std::cerr << "Not a FTP protocol URL\n";
    exit(1);
  }

  const std::string fullArgument(argv[2] + (sizeof("ftp://") - 1));
  size_t usernamePWSplit;
  if ((usernamePWSplit = fullArgument.find("@")) != fullArgument.npos) {
    // Found username and/or password
    const std::string usernameAndPW = fullArgument.substr(0, usernamePWSplit);
    size_t passwordSplit;
    if ((passwordSplit = usernameAndPW.find(':')) != usernameAndPW.npos) {
      // Found Password
      // Username
      argArray[1] = usernameAndPW.substr(0, passwordSplit);
      // Password
      argArray[2] = usernameAndPW.substr(passwordSplit + 1);

    } else {
      // Only has the username
      argArray[1] = usernameAndPW;
    }
  }
  // Extract Hostname
  size_t hostNameSplit;
  if ((hostNameSplit = fullArgument.find("/")) != fullArgument.npos) {
    // Hostname Found, look for port number
    const std::string hostnameAndPort = fullArgument.substr(
        usernamePWSplit != fullArgument.npos ? (usernamePWSplit + 1) : 0,
        hostNameSplit);
    size_t portNumberSplit;
    if ((portNumberSplit = hostnameAndPort.find(":")) != hostnameAndPort.npos) {
      // Port Number found
      // Hostname
      argArray[3] = hostnameAndPort.substr(0, portNumberSplit);
      // Port Number
      argArray[4] = hostnameAndPort.substr(
          portNumberSplit + 1,
          usernamePWSplit != fullArgument.npos
              ? hostnameAndPort.size() - 2 - portNumberSplit
              : hostnameAndPort.size() - 1 - portNumberSplit);
    } else {
      // No Port Number found
      argArray[3] = hostnameAndPort;
    }
  } else {
    std::cerr << "Couldn't find hostname while parsing string\n" << "\n";
    exit(1);
  }

  // First Argument
  argArray[5] = fullArgument.substr(hostNameSplit);
  if (argc > 3) argArray[6] = argv[3];
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "Expected 3 Arguments with program call \n";
    exit(1);
  }
  // $ ./ftpClient [operation] [param1] [param2]

  int sockfd = -1;
  // command, username, password, ftp hostname, port, firstArg, secondArg
  // only command, hostname and firstArg - everything else is optional
  std::array<std::string, 7> argArray;
  parseInputParameters(argc, argv, argArray);

  //   for (const std::string& s : argArray) {
  //     std::cout << s << "\n";
  //   }
  setupConnection(&sockfd, argArray);
  initMessage(sockfd, argArray);

  close(sockfd);
  return 0;
}