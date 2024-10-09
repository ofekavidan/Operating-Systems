#include "server.h"
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <cstring>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <iostream>
#include <fstream>

#define MAX_CLIENTS 3
#define SYSTEM_ERR_MSG "system error: "
#define SOCKET_ERROR_MSG "socket error"
#define GETHOSTBYNAME_ERR_MSG "gethostbyname error"
#define BIND_ERR_MSG "bind error"
#define FTOK_ERR_MSG "ftok error"
#define SHMGET_ERR_MSG "shmget error"
#define INFO_FILE_ERR_MSG "infofile error"
#define IP_ERR_MSG "ip error"
#define SELECT_TIMEOUT_SEC 120
#define SELECT_ERR_MSG "select error"
#define ACCEPT_ERR_MSG "accept error"
#define INFO_FILE_REMOVE_ERR_MSG "infofile remove error"
#define SLEEP_TIME 10

void server_error_and_exit (const std::string &msg)
{
  std::cerr << SYSTEM_ERR_MSG << msg << std::endl;
  exit (1);
}

void
start_communication (const server_setup_information &setup_info, live_server_info &server)
{
  //init server socket
  int welcome_socket_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (welcome_socket_fd == -1) {
    server_error_and_exit (SOCKET_ERROR_MSG);
  }

  //find ip
  char host_name[MAXHOSTNAME + 1];
  gethostname (host_name, MAXHOSTNAME); // gets the name of the host into host name
  hostent *h = gethostbyname (host_name); // hostnet with the ip
  if (h == nullptr) {
    close (welcome_socket_fd);
    server_error_and_exit (GETHOSTBYNAME_ERR_MSG);
  }

  //sa init:
  sockaddr_in sa{};
  memset (&sa, 0, sizeof (struct sockaddr_in));
  sa.sin_family = AF_INET;
  memcpy (&sa.sin_addr, h->h_addr, h->h_length);
  sa.sin_port = htons (setup_info.port);

  //bind the socket to the ip
  if (bind (welcome_socket_fd, (struct sockaddr *) &sa, sizeof (struct sockaddr_in))< 0) {
    close (welcome_socket_fd);
    server_error_and_exit (BIND_ERR_MSG);
  }

  listen (welcome_socket_fd, MAX_CLIENTS);

  //allocate shared memory
  key_t unique_key = ftok (setup_info.shm_pathname.c_str (), setup_info.shm_proj_id);
  if (unique_key == -1) {
    close (welcome_socket_fd);
    server_error_and_exit (FTOK_ERR_MSG);
  }

  int shared_memory = shmget (unique_key, SHARED_MEMORY_SIZE,IPC_CREAT | 0666);
  if (shared_memory == -1) {
    close (welcome_socket_fd);
    server_error_and_exit (SHMGET_ERR_MSG);
  }
  server.server_fd = welcome_socket_fd;
  server.shmid = shared_memory;
}

void
create_info_file (const server_setup_information &setup_info, live_server_info &server)
{
  //create file
  std::string filePath =
      setup_info.info_file_directory + "/" + setup_info.info_file_name;
  std::ofstream outputFile (filePath);
  if (!outputFile){
    close (server.server_fd);
    shmctl (server.shmid, IPC_RMID, NULL);
    server_error_and_exit (INFO_FILE_ERR_MSG);
  }

  //get ip
  char host_name[MAXHOSTNAME + 1];
  gethostname (host_name, MAXHOSTNAME); // gets the name of the host into host name
  hostent *h = gethostbyname (host_name); // hostnet with the ip
  if (h == nullptr){
    close (server.server_fd);
    shmctl (server.shmid, IPC_RMID, NULL);
    server_error_and_exit (GETHOSTBYNAME_ERR_MSG);
  }
  char *ip = inet_ntoa (*((struct in_addr *) h->h_addr));
  if (ip == nullptr){
    close (server.server_fd);
    shmctl (server.shmid, IPC_RMID, NULL);
    server_error_and_exit (IP_ERR_MSG);
  }

  //write to the file:
  outputFile << ip << std::endl;
  outputFile << setup_info.port << std::endl;
  outputFile << setup_info.shm_pathname << std::endl;
  outputFile << setup_info.shm_proj_id << std::endl;

  // close the file
  outputFile.close ();
  server.info_file_path = filePath;
}

void get_connection (live_server_info &server) {
    // we've used a code from TA7 slide 60
    fd_set readfds;
    FD_SET(server.server_fd, &readfds);

    timeval time{};
    time.tv_sec = SELECT_TIMEOUT_SEC;
    time.tv_usec=0;
    int select_res = select(server.server_fd + 1, &readfds, NULL, NULL, &time);

    if (select_res == 0) {//timed out- no connection
        server.client_fd = -1;
        return;
    } else if (select_res == -1) {//coudlnt connect
        shutdown(server);
        server_error_and_exit(SELECT_ERR_MSG);
    }

    int client_socket_fd;
    if ((client_socket_fd = accept(server.server_fd, NULL, NULL)) < 0) {
        // Error accepting client connection
        shutdown(server);
        server_error_and_exit(ACCEPT_ERR_MSG);
    }
    server.client_fd = client_socket_fd;
}

void write_to_socket (const live_server_info &server, const std::string &msg)
{
  write (server.client_fd, msg.c_str (), msg.length ());
}

void write_to_shm (const live_server_info &server, const std::string &msg)
{
  char *str = (char *) shmat (server.shmid, (void *) 0, 0);

  std::strncpy (str, msg.c_str (), SHARED_MEMORY_SIZE - 1);
  str[SHARED_MEMORY_SIZE - 1] = '\0';

  // Detach from the shared memory segment
  shmdt (str);
}

void shutdown (const live_server_info &server)
{
  if (server.client_fd != -1) {
    close(server.client_fd);
  }
  close (server.server_fd);
  shmctl (server.shmid, IPC_RMID, NULL);
  if (std::remove (server.info_file_path.c_str ()) != 0)
  {
    server_error_and_exit (INFO_FILE_REMOVE_ERR_MSG);
  }
}

void
run (const server_setup_information &setup_info, const std::string &shm_msg, const std::string &socket_msg)
{
  live_server_info server={0,0,0, ""};
  start_communication (setup_info, server);
  create_info_file (setup_info, server);
  write_to_shm (server, shm_msg);
  get_connection (server);
  if (server.client_fd != -1)
  {
    write_to_socket (server, socket_msg);
  }
  sleep (SLEEP_TIME);
  shutdown (server);
}