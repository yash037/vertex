#include "../include/Server.h"
#include "../include/CommandHandler.h"
#include "../include/Database.h"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <vector>
#include <thread>
#include <cstring>
#include <signal.h>


//created global pointer (signal handling)
static Server* globalServer = nullptr;

void signalHandler(int signum){
    if(globalServer){
        std::cout<<"\n came signal "<<signum<<", shutting down.. \n";
        globalServer->shutdown();
    }
    //returning signum integer to os after exit obv not exit(0)
    exit(signum);
}

void Server::setupSignalHandler() {
    signal(SIGINT, signalHandler);
}
Server::Server(int port) : port(port), server_socket(-1), running(true){
    globalServer = this;
    setupSignalHandler();
}

void Server::shutdown(){
    //socket server shutdown
    running = false; //atomic op
    if(server_socket != -1){
        //persisting database
        if(Database::getInstance().dump("dump.my_rdb")){
            std::cout<<"persistance process success \n";
        }
        else{
            std::cerr<<"Error dumping database \n";
        }
        close(server_socket); //close sys call
    }
    std::cout<<"server shutdown complete \n";
}

void Server::run(){
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socket < 0){
        std::cerr<<"can not create socket \n";
        return;
    }

    int opt = 1; //option level for tcp protocol going to be used in socket
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{}; //sockaddr_in struct instance for socket config
    
    serverAddr.sin_family = AF_INET; //ipv4
    serverAddr.sin_port = htons(port); //convert to network system
    serverAddr.sin_addr.s_addr = INADDR_ANY; //can accept from any ip 

    if(bind(server_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0){
        std::cerr<<"socket bind failed in server \n";
        return;
    }

    //max pending connections -> 10
    //making socket passive -> later on accept sys call
    //clien::connect() and server::accept() connection between these sates are backlog thats why set 10
    if(listen(server_socket, 10) < 0){
        std::cerr<<"server listen errror \n";
        return;
    }

    std::cout<<" server listening on port "<<port<<"\n";

    std::vector<std::thread> threads;
    CommandHandler cmdHandler; //implement later 

    while (running) {
        int client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket < 0) {
            if (running) 
                std::cerr << "Error Accepting Client Connection\n";
            break;
        }

        //setup a beautiful thread lol for this shit
        threads.emplace_back([client_socket, &cmdHandler](){
            char buffer[1024]; //buffer to recv client msg
            while (true) {
                memset(buffer, 0, sizeof(buffer)); //set buffer memory block to 0
                int bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0); //recv sys call
                if (bytes <= 0) break; //if recv return bytes count <= 0
                std::string request(buffer, bytes); //might read garbage in string request thats why bytes must be specified
                std::string response = cmdHandler.processCommand(request);
                send(client_socket, response.c_str(), response.size(), 0);
            }
            close(client_socket);
        });
    }
    
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    // Before shutdown, persist the database
    if (Database::getInstance().dump("dump.my_rdb"))
        std::cout << "Database Dumped to dump.my_rdb\n";
    else 
        std::cerr << "Error dumping database\n";
}