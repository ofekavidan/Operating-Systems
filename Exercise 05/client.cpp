#include "client.h"
#include <iostream>
#include <string>
#include <dirent.h>
#include <fstream>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <algorithm>

#define MAX_CLIENTS 3
#define SYSTEM_ERR_MSG "system error: "
#define OPEN_FILE_ERR_MSG "opening file error"
#define SOCKET_ERR_MSG "socket error"
#define SELECT_ERR_MSG "select error"
#define TOTAL_SERVERS_MSG "Total Servers: "
#define HOST_MSG "Host: "
#define CONTAINERS_MSG "Container: "
#define VM_MSG "VM: "
#define MESSAGES_MSG "Messages:"
#define CLOSE_SERVERS_ERR_MSG "close server error"


struct sort_comparator{
    bool operator() (const live_server_info& first, const live_server_info& second) const{
        return first.info_file_path < second.info_file_path;
    }
};

void client_error_and_exit(const std::string& msg){
  std::cerr << SYSTEM_ERR_MSG << msg << std::endl;
  exit (1);
}

void read_info_file(const std::string& info_file_path,std::string& ip,std::string& port,std::string& shm_pathname,
                    std::string& shm_proj_id){
    std::ifstream file (info_file_path);
    if (!file) {
      client_error_and_exit (OPEN_FILE_ERR_MSG);
    }

    // Read file
    int counter = 0;
    std::string line;

    std::string info_arr[4];

    while (std::getline (file, line) and counter < 4)
    { //<IP> <port> <shm_pathname> <shm_proj_id>
        info_arr[counter] = line;
        counter++;
    }
    ip = info_arr[0];
    port=info_arr[1];
    shm_pathname=info_arr[2];
    shm_proj_id=info_arr[3];
    file.close ();
}

void connect_via_socket(std::string ip,std::string port,live_server_info& cur_server_info){
  int client_socket_fd = socket (AF_INET, SOCK_STREAM, 0);
  if (client_socket_fd == -1)
  {
    client_error_and_exit (SOCKET_ERR_MSG);
  }
  //init sa:
  sockaddr_in sa{};
  memset (&sa, 0, sizeof (struct sockaddr_in));
  sa.sin_family = AF_INET;
  inet_pton (AF_INET, ip.c_str (), &(sa.sin_addr));
  sa.sin_port = htons (std::stoi (port));

  //try to get a connection to the cur server
  fd_set writefds;
  FD_SET(client_socket_fd, &writefds);

  timeval time{};
  time.tv_sec = 5;
  time.tv_usec=0;
  int select_res=select (client_socket_fd + 1, NULL, &writefds, NULL, &time);
  if (select_res== 0)
  {
    cur_server_info.client_fd = -1;
    return;
  }
  else if(select_res==-1){
    client_error_and_exit (SELECT_ERR_MSG);
  }

  if (connect (client_socket_fd, (struct sockaddr *)(&sa), sizeof (sa))<0) {
    cur_server_info.client_fd = -1;
  }

  cur_server_info.client_fd=client_socket_fd;
}

void connect_via_shared_memory(const std::string& shm_pathname,
                               const std::string& shm_proj_id, live_server_info& cur_server_info){
  key_t key = ftok (shm_pathname.c_str (), stoi (shm_proj_id));
  int shmid = shmget (key, SHARED_MEMORY_SIZE, 0666);
  cur_server_info.shmid = shmid;
}

int
count_servers (const std::string &client_files_directory, std::vector<live_server_info> &servers)
{
  // open directory
  DIR *dir = opendir (client_files_directory.c_str ());
  if (dir == nullptr)
  {
    return -1;
  }
  struct dirent *entry;
  while ((entry = readdir (dir)) != nullptr)
  {
    // Check it is a basic file (not a directory or a special entry like "." or "..")
    if (entry->d_type == DT_REG)
    {
      std::string filename = entry->d_name;
      std::string filePath = client_files_directory + "/" + filename;

        std::string ip;
        std::string port;
        std::string shm_pathname;
        std::string shm_proj_id;
        read_info_file(filePath,ip,port,shm_pathname,shm_proj_id);

      live_server_info cur_server_info{};
      connect_via_socket (ip,port,cur_server_info);

      connect_via_shared_memory(shm_pathname,shm_proj_id,cur_server_info);

      cur_server_info.server_fd=-1;
      cur_server_info.info_file_path = filePath;
      servers.push_back (cur_server_info);
    }
  }
  std::sort(servers.begin(),servers.end(),sort_comparator());
  closedir (dir);
  return servers.size ();
}

void print_server_infos (const std::vector<live_server_info> &servers)
{
  // VM- CANT SOCKET CANT SHARED
  // HOST- CAN RUN SHARED AND SOCKET
  // CONTAINER CANT SHARED MEMORY CAN SOCKET
    int host_server_num=0;
    int container_server_num=0;
    int vm_server_num=0;
    std::vector<std::string > msg_vec;
    for(auto server:servers){
        std::string shm_msg;
        std::string sock_msg;

        get_message_from_shm(server,shm_msg);
        get_message_from_socket(server,sock_msg);

        if(!shm_msg.empty()){
            msg_vec.push_back(shm_msg);
        }

        if(!sock_msg.empty()){
            msg_vec.push_back(sock_msg);
        }

        if(!sock_msg.empty() and !shm_msg.empty()){
            host_server_num++;
        }
        else if (sock_msg.empty() and shm_msg.empty()){
            vm_server_num++;
        }
        else{
            container_server_num++;
        }
    }
    std::cout << TOTAL_SERVERS_MSG << servers.size () << std::endl;
    std::cout<<HOST_MSG<<host_server_num<<std::endl;
    std::cout<<CONTAINERS_MSG<<container_server_num<<std::endl;
    std::cout<<VM_MSG<<vm_server_num<<std::endl;
    std::cout<<MESSAGES_MSG;
    for(const auto& msg:msg_vec){
        std::cout<<" "<<msg;
    }
    std::cout<<std::endl;
}


void get_message_from_socket (const live_server_info &server, std::string &msg)
{
  char msg_buf[SHARED_MEMORY_SIZE];

  while((read (server.client_fd, msg_buf, sizeof (msg_buf) - 1))>0){
      msg+=msg_buf;
      std::fill(msg_buf,msg_buf+SHARED_MEMORY_SIZE,0);
  }
}

void get_message_from_shm (const live_server_info &server, std::string &msg)
{
    // Read from shared memory
    char* str = (char*)shmat(server.shmid, (void*)0, 0);
    if(str== (void*) (-1)){
      return;
    }
    msg = str;
    shmdt(str);
}

void disconnect (const std::vector<live_server_info> &servers)
{
  for (auto &server: servers)
  {
    if (server.client_fd!=-1 && close (server.client_fd) == -1)
    {
      client_error_and_exit (CLOSE_SERVERS_ERR_MSG);
    }
  }
}

void run (const std::string &client_files_directory)
{
  std::vector<live_server_info> server_vec;
  count_servers (client_files_directory, server_vec);
  print_server_infos (server_vec);
  disconnect (server_vec);
}