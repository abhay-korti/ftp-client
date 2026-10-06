#include <iostream>
#include <array>
#include <string>
#include <netdb.h>
#include <unistd.h>
#include <regex>
#include <fstream>
#include <string.h>
#include <arpa/inet.h>
#include <unordered_map>

#define BUFFER_SIZE 4096

void sendMessage(int sockfd, char* message);
void dataTransferPrep(int sockfd);
int sendMessageStatus(int sockfd);
int dataChannelStart(int sockfd);
void runCommand(int sockfd, std::array<std::string, 8>& argArray);
void initMessage(int sockfd, std::array<std::string, 8>& argArray);
void parseInputParameters(int argc, char** argv,
                          std::array<std::string, 8>& argArray);
void readAndSendFile(int dataChannelfd, char* filePath);

void sendMessage(int sockfd, char* message) {
  if (send(sockfd, message, strlen(message), 0) < 0) {
    std::cout << "Init Error: Error sending " << message
              << " message to server\n";
    close(sockfd);
    exit(1);
  }
}

int sendFileChunk(int sockfd, char* message, ssize_t n) {
  if (send(sockfd, message, n, 0) < 0) {
    std::cout << "Init Error: Error sending " << message
              << " message to server\n";
    close(sockfd);
    return -1;
  }
  return 0;
}

void readAndSendFile(int dataChannelfd, const char* filePath) {
  std::fstream uploadFile(filePath, std::ios::in | std::ios::binary);
  if (!uploadFile.is_open()) {
    std::cout << "STOR Error: Failed to open the file\n";
    exit(1);
  }
  char buffer[4 * BUFFER_SIZE];
  while (uploadFile.read(buffer, sizeof(buffer)) || uploadFile.gcount() > 0) {
    std::streamsize n = uploadFile.gcount();
    if (sendFileChunk(dataChannelfd, buffer, n) != 0) {
      std::cout << "STOR Error: Failed to send File Chunk\n";
      exit(1);
    }
  }
  if (uploadFile.bad()) {
    std::cout << "STOR Error: Failed process file\n";
    exit(1);
  }
  uploadFile.close();
}

void receiveAndWriteFile(int dataChannelfd, const char* filePath) {
  std::ofstream writeFile(filePath, std::ios::binary | std::ios::trunc);
  if (!writeFile.is_open()) {
    std::cout << "RETR Error: Failed to open the file\n";
    exit(1);
  }
  char buf[4 * BUFFER_SIZE];
  ssize_t n;
  while ((n = recv(dataChannelfd, buf, sizeof(buf), 0)) != 0) {
    if (n < 0) {
      if (errno == EINTR) continue;
      perror("RETR Error: recv failed");
      close(dataChannelfd);
      exit(1);
    }
    writeFile.write(buf, n);
    if (!writeFile) {
      std::cout << "RETR Error: Failed to write to the file\n";
      close(dataChannelfd);
      exit(1);
    }
  }
  writeFile.close();
  close(dataChannelfd);
}

void dataTransferPrep(int sockfd) {
  char typeMessage[] = "TYPE I\r\n";
  sendMessage(sockfd, typeMessage);
  sendMessageStatus(sockfd);

  char modeMessagge[] = "MODE S\r\n";
  sendMessage(sockfd, modeMessagge);
  sendMessageStatus(sockfd);

  char structureCodeMessage[] = "STRU F\r\n";
  sendMessage(sockfd, structureCodeMessage);
  sendMessageStatus(sockfd);
}

int sendMessageStatus(int sockfd) {
  char buf[BUFFER_SIZE];
  memset(buf, 0, BUFFER_SIZE);
  ssize_t bufferBytesUsed = 0;
  while (1) {
    char* pointer = strstr(buf, "\r\n");
    if (pointer) break;
    if (bufferBytesUsed == sizeof(buf)) {
      std::cout << "Recv Error: Recv Buffer Full\n";
      close(sockfd);
      return -1;
    }
    ssize_t n = recv(sockfd, buf + bufferBytesUsed,
                     sizeof(buf) - bufferBytesUsed - 1, 0);
    if (n < 0) {
      std::cout << "Recv Error: Error Receiving Data\n";
      close(sockfd);
      return -1;
    }
    if (n == 0) break;
    bufferBytesUsed += n;
  }

  if (buf[0] == '2')
    return 1;
  else
    return 0;
}

int dataChannelStart(int sockfd) {
  char pasvMessage[] = "PASV\r\n";
  sendMessage(sockfd, pasvMessage);
  char buf[BUFFER_SIZE];
  memset(buf, 0, BUFFER_SIZE);
  ssize_t bufferBytesUsed = 0;
  while (1) {
    char* pointer = strstr(buf, "\r\n");
    if (pointer) break;
    if (bufferBytesUsed == sizeof(buf)) {
      std::cout << "Recv Error: Recv Buffer Full\n";
      close(sockfd);
      return -1;
    }
    ssize_t n = recv(sockfd, buf + bufferBytesUsed,
                     sizeof(buf) - bufferBytesUsed - 1, 0);
    if (n < 0) {
      std::cout << "Recv Error: Error Receiving Data\n";
      close(sockfd);
      return -1;
    }
    if (n == 0) break;
    bufferBytesUsed += n;
  }

  buf[bufferBytesUsed] = '\0';
  std::string response(buf);
  std::regex pattern("\\d+,\\d+,\\d+,\\d+,\\d+,\\d+");
  auto patternBegin =
      std::sregex_iterator(response.begin(), response.end(), pattern);
  auto patternEnd = std::sregex_iterator();
  std::smatch match = *patternBegin;
  std::stringstream toBeSplit(match.str());
  std::vector<unsigned int> numbers;
  std::string token;
  while (std::getline(toBeSplit, token, ',')) {
    numbers.push_back(std::stoi(token));
  }

  std::string dataChannelIPAddress = "";

  for (int i = 0; i < 4; i++) {
    dataChannelIPAddress += std::to_string(numbers[i]) + ".";
  }
  dataChannelIPAddress.pop_back();
  const unsigned int dataChannelPortNumber = (numbers[4] << 8) + numbers[5];

  int dataChannelfd;
  while ((dataChannelfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) continue;

  struct sockaddr_in dataAddress = {0};
  dataAddress.sin_family = AF_INET;
  dataAddress.sin_port = htons(dataChannelPortNumber);
  inet_pton(AF_INET, dataChannelIPAddress.data(), &dataAddress.sin_addr);

  if (connect(dataChannelfd, (struct sockaddr*)&dataAddress,
              sizeof(dataAddress)) == -1) {
    std::cout << "Data Channel Error: Error Establishing a Connection\n";
    close(dataChannelfd);
    return -1;
  }

  return dataChannelfd;
}

void runCommand(int sockfd, std::array<std::string, 8>& argArray) {
  std::unordered_map<std::string, const char*> commandMap;
  commandMap["rmdir"] = "RMD";
  commandMap["rm"] = "DELE";
  commandMap["mkdir"] = "MKD";
  commandMap["ls"] = "LIST";
  if (argArray[7] == "STOR")
    commandMap["cp"] = "STOR";
  else
    commandMap["cp"] = "RETR";

  char message[BUFFER_SIZE];
  sprintf(message, "%s %s\r\n", commandMap[argArray[0]], argArray[5].data());

  if ((strcmp("RMD", commandMap[argArray[0]]) == 0) ||
      (strcmp("MKD", commandMap[argArray[0]]) == 0) ||
      (strcmp("DELE", commandMap[argArray[0]]) == 0)) {
    sendMessage(sockfd, message);
    sendMessageStatus(sockfd);
  } else if ((strcmp(commandMap[argArray[0]], "LIST") == 0)) {
    dataTransferPrep(sockfd);
    int dataChannelfd = dataChannelStart(sockfd);
    sendMessage(sockfd, message);
    sendMessageStatus(sockfd);
    sendMessageStatus(dataChannelfd);
    sendMessageStatus(sockfd);
  } else if ((strcmp(commandMap[argArray[0]], "STOR") == 0)) {
    dataTransferPrep(sockfd);
    int dataChannelfd = dataChannelStart(sockfd);
    // Send STOR Request
    sendMessage(sockfd, message);
    // Receive OK to Send
    sendMessageStatus(sockfd);
    // Send Data
    readAndSendFile(dataChannelfd, argArray[6].c_str());
    // Close the Connection
    char quitMessage[] = "QUIT\r\n";
    sendMessage(sockfd, quitMessage);
    close(dataChannelfd);
    sendMessageStatus(sockfd);
  } else if ((strcmp(commandMap[argArray[0]], "RETR") == 0)) {
    dataTransferPrep(sockfd);
    int dataChannelfd = dataChannelStart(sockfd);
    sendMessage(sockfd, message);
    sendMessageStatus(sockfd);
    receiveAndWriteFile(dataChannelfd, argArray[6].c_str());
    sendMessageStatus(sockfd);
    char quitMessage[] = "QUIT\r\n";
    sendMessage(sockfd, quitMessage);
    sendMessageStatus(sockfd);
    close(dataChannelfd);
  } else {
    std::cout << "Command Not Recognized\n";
    exit(1);
  }
}

void setupConnection(int* sockfd, std::array<std::string, 8>& argArray) {
  struct addrinfo hints, *p, *start;
  int status;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = AF_UNSPEC;
  if ((status = getaddrinfo(argArray[3].c_str(), argArray[4].c_str(), &hints,
                            &start)) != 0) {
    std::cout << "Encountered an error trying to establish connection\n";
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
    std::cout << "None of the attempts made resulted in a valid connection\n";
    freeaddrinfo(start);
    exit(1);
  }
  freeaddrinfo(start);
}

void initMessage(int sockfd, std::array<std::string, 8>& argArray) {
  // Receive the Hello Message
  sendMessageStatus(sockfd);

  char userMessage[BUFFER_SIZE];
  sprintf(userMessage, "USER %s\r\n", argArray[1].c_str());
  sendMessage(sockfd, userMessage);
  sendMessageStatus(sockfd);

  char passwordMessage[BUFFER_SIZE];
  if (argArray[1] != "anonymous") {
    sprintf(passwordMessage, "PASS %s\r\n", argArray[2].c_str());
  } else {
    sprintf(passwordMessage, "PASS\r\n");
  }
  sendMessage(sockfd, passwordMessage);
  sendMessageStatus(sockfd);
}

void parseInputParameters(int argc, char** argv,
                          std::array<std::string, 8>& argArray) {
  // Default arguments
  char* pointer = strstr(argv[2], "ftp://");
  if (!pointer) {
    argArray[7] = "STOR";
    std::swap(argv[2], argv[3]);
  } else {
    argArray[7] = "RETR";
  }

  argArray[0] = argv[1];
  // Command     ^
  argArray[1] = "anonymous";
  // Username    ^
  argArray[2] = "";
  // Password    ^
  argArray[3] = "";
  // Hostname    ^
  argArray[4] = "21";
  // Port Number ^
  argArray[5] = "/";
  // First Arg   ^
  argArray[6] = "";
  // Second Arg  ^

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
        usernamePWSplit != fullArgument.npos
            ? hostNameSplit - (usernamePWSplit + 1)
            : hostNameSplit - 0);
    size_t portNumberSplit;
    if ((portNumberSplit = hostnameAndPort.find(":")) != hostnameAndPort.npos) {
      // Port Number found
      // Hostname
      argArray[3] = hostnameAndPort.substr(0, portNumberSplit);
      // Port Number
      argArray[4] = hostnameAndPort.substr(
          portNumberSplit + 1, hostnameAndPort.size() - 1 - portNumberSplit);
    } else {
      // No Port Number found
      argArray[3] = hostnameAndPort;
    }
  } else {
    std::cout << "Couldn't find hostname while parsing string - Missing "
                 "Starting Directory\n";
    exit(1);
  }

  // First Argument
  argArray[5] = fullArgument.substr(hostNameSplit);
  if (argc > 3) argArray[6] = argv[3];
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cout << "Expected 3 Arguments with program call \n";
    exit(1);
  }
  // $ ./ftpClient [operation] [param1] [param2]

  int sockfd = -1;
  std::array<std::string, 8> argArray;
  parseInputParameters(argc, argv, argArray);

  setupConnection(&sockfd, argArray);
  initMessage(sockfd, argArray);
  runCommand(sockfd, argArray);

  char quitMessage[] = "QUIT\r\n";
  sendMessage(sockfd, quitMessage);
  sendMessageStatus(sockfd);
  close(sockfd);
  return 0;
}