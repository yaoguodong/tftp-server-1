/*  
 *  20. sept 2015
 *  Reykjavik University - Computer Networks
 *  PROGRAMMING ASSIGNMENT 1: TRIVIAL FILE TRANSFER PROTOCOL
 */

#include <assert.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>

/*----How to convert values between host and network byte order----
  The htonl() function converts the unsigned integer hostlong
  from host byte order to network byte order.

  The htons() function converts the unsigned short integer hostshort
  from host byte order to network byte order.

  The ntohl() function converts the unsigned integer netlong
  from network byte order to host byte order.

  The ntohs() function converts the unsigned short integer netshort
  from network byte order to host byte order.
------------------------------------------------------------------*/

/*
                   TFTP Formats

   Type   Op #     Format without header

          2 bytes    string   1 byte     string   1 byte
          -----------------------------------------------
   RRQ/  | 01/02 |  Filename  |   0  |    Mode    |   0  |
   WRQ    -----------------------------------------------
          2 bytes    2 bytes       n bytes
          ---------------------------------
   DATA  | 03    |   Block #  |    Data    |
          ---------------------------------
          2 bytes    2 bytes
          -------------------
   ACK   | 04    |   Block #  |
          --------------------
          2 bytes  2 bytes        string    1 byte
          ----------------------------------------
   ERROR | 05    |  ErrorCode |   ErrMsg   |   0  |
          ----------------------------------------
 */

struct message {
  short opcode;
  char buffer[514];
};

struct datapacket {
  short opcode;
  short block;
  char data[512];
};

struct ack {
  short opcode;
  short block;
};

char folderpath[60];
void tftp_send(struct sockaddr_in client, struct message);

int main(int argc, char **argv)
{
        int sockfd;
        struct sockaddr_in server, client;
        struct message message;

        if(argc < 3){
          exit(EXIT_FAILURE);
        }

        // Create and bind a UDP socket

        if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
          exit(EXIT_FAILURE);
        }

        // Load system information into socket data structure

        memset(&server, 0, sizeof(server)); /* Clear structure memory */
        server.sin_family = AF_INET; /* IPv4 */
        server.sin_addr.s_addr = htonl(INADDR_ANY); /* Bind to all interfaces (0) */
        server.sin_port = htons(strtol(argv[1], NULL, 0)); /* Set port number */

        // Bind the socket to a local socket address

        if(bind(sockfd, (struct sockaddr*) &server, (socklen_t) sizeof(server)) < 0){
          perror("bind");
          exit(EXIT_FAILURE);
        }


        /**********************
             Server loop
        ***********************/


        for (;;) {
          int rc;
          int length = (int) sizeof(struct sockaddr_in);

          rc = recvfrom(sockfd, &message, sizeof(struct message), 0,
                        (struct sockaddr*)&client, (socklen_t*)&length);
          if(rc == 0){
            printf("Error! Socket closed\n");
          } else if (rc == -1){
            printf("Socket error!\n");
            exit(EXIT_FAILURE);
          }

          /*---------------------------
          opcode  operation
            1     Read request (RRQ)
            2     Write request (WRQ)
            3     Data (DATA)
            4     Acknowledgment (ACK)
            5     Error (ERROR)
          ----------------------------*/

          switch(ntohs(message.opcode)){
          case 1: // (RRQ)
            realpath(argv[2], folderpath);
            printf("file \"%s\" requested from %s:%d\n",
                   message.buffer, inet_ntoa(client.sin_addr), ntohs(client.sin_port));
            tftp_send(client, message);
            break;
          case 2: // (WRQ)
            printf("Write request not allowed!\n");
            close(sockfd);
            exit(EXIT_FAILURE);
            break;
          default:
            printf("Error code: %d\n", ntohs(message.opcode));
            close(sockfd);
            exit(EXIT_FAILURE);
            break;
          }
        }
}

void tftp_send(struct sockaddr_in client, struct message message){
  int length = (int) sizeof(struct sockaddr_in);

  // Initial connection protocol

  int TIDport = (rand() % 63500) + 1337;

  struct sockaddr_in server;
  memset(&server, 0, sizeof(server));
  server.sin_family = AF_INET; /* IPv4 */
  server.sin_port = htons(TIDport); /* Set port number */
  server.sin_addr.s_addr = htonl(INADDR_ANY); /* Bind to all interfaces (0) */

  int sockfd;
  if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
    close(sockfd);
    exit(EXIT_FAILURE);
  }
  if(bind(sockfd, (struct sockaddr*) &server, (socklen_t) sizeof(server)) < 0){
    close(sockfd);
    perror("bind");
    exit(EXIT_FAILURE);
  }

  // Open the file

  char filepath[80];
  sprintf(filepath, "%s/%s", folderpath, message.buffer);
  char* mode = &message.buffer[strlen(message.buffer) + 1];
  char check[80];
  char oct[10];
  strcpy(oct, "octet");

  FILE* file;
  realpath(filepath, check);

  if(strncmp(check, folderpath, strlen(folderpath))){
    printf("Invalid path! Try again..\n");
    return;
  }
  else {
    if(!strcmp(mode, oct)){
      file = fopen(filepath, "rb");
    } else {
      file = fopen(filepath, "r");
    }
  }

  if(file == NULL){
    printf("Could not open the file!\n");
    return;
  }

  // Create the data packet

  struct datapacket data;
  struct ack msg;
  int block = 1;
  data.block = htons(1);
  data.opcode = htons(3);

  int size = fread(data.data, 1, 512, file);

  // Send the requested file

  for(;;) {

    // Send first packet

    sendto(sockfd, &data, size + 4, 0,
           (struct sockaddr*)&client,(int) sizeof(struct sockaddr_in));

    // Wait for response msg

    int rc;
    if((rc = recvfrom(sockfd, &msg, sizeof(struct message), 0,
                      (struct sockaddr*)&client, (socklen_t*)&length)) == -1){
      break;
    }

    if(ntohs(msg.opcode) == 4 && ntohs(msg.block) == block){
      if(size < 512){
        break;
      }
      block++;
      data.block = htons(block);
      size = fread(data.data, 1, 512, file);
    }

  }

  fclose(file);
  close(sockfd);
}
