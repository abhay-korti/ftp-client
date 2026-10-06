# ftp-client

Approach - 
The FTP client implementation follows the recommended appraoach to building the required functions for ftp-server interactions. The command line arguments are parsed and and functions are run based on the command issued by the user. 

PASV command is issued anytime there is download/upload of information, which is then used in commands like LIST, STOR and RETR. 

Challenges - 
Handling two sockets and ensuring the right message went to the right socket and receving the right amount of messages to ensure that there wasn't any mistach between server and the client was definitely challenging. 

Testing - 
Code was mostly manually tested along with using tools like strace and valgrind to ensure that network calls were being made and no bytes were lost. 
