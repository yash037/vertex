#include <iostream>
#include "../include/Server.h"
#include "../include/Database.h"
#include <thread>
#include <chrono>

// screw the lambda function syntax using normal
void persistDatabase() {
    while(true){
        std::this_thread::sleep_for(std::chrono::seconds(300));
        if(!Database::getInstance().dump("dump.my_rcb")){
            std::cerr<<"error dumping database\n";
        }
        else{
            std::cout<<"database dumped to dump.my_rdb\n";
        }
    }
}

// In main()
std::thread persistanceThread(persistDatabase);



int main(int argc, char *argv[]){
    int port = 6440;
    if(argc >= 2) port = std::stoi(argv[1]);


    //singleton pattern trololo
    if(Database::getInstance().load("dump.my_rdb")){
        std::cout<<"database loaded from dump.my_rdb\n";
    }
    else{
        std::cout<<"no dump file found database not loaded \n";
    }

    Server server(port);

    //dump database every 180 seconds
    std::thread persistanceThread(persistDatabase);

    //so dont join this thread later, this thread run independently forever (evern main returns)
    persistanceThread.detach();

    server.run();
    return 0;
}