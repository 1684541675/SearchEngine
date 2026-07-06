#include "Configuration.h"
#include "EchoServer.h"

using namespace searchengine;

void runServer()
{
    const string ip = Configuration::getInstance().getConfigMap()["ip"];
    const unsigned short port = (unsigned short)stoul(Configuration::getInstance().getConfigMap()["port"]);

    EchoServer server(ip, port);
    server.start();
}

int main()
{
    runServer();
    return 0;
}
