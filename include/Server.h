#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <atomic>



class Server{
public:
    Server(int port);
    void run();
    void shutdown();

private:
    int port;
    int server_socket;
    std::atomic<bool> running;

    //signal handling for good healthy shutdown
    void setupSignalHandler();
};

#endif