#include "websocketpp/server.hpp"
#include "websocketpp/config/asio_no_tls.hpp"
#include <string>
#include <map>
#include <ostream>
#include "./LogStream.hpp"


class WebsocketServer {
public:
    static bool init();
    static void run();
    static void stop();

    static bool sendClose(std::string id);
    static bool sendData(std::string id, std::string data);


private:
    static bool getWebsocket(const std::string &id, websocketpp::connection_hdl &hdl);
    static pthread_rwlock_t websocketsLock;
    static std::map<std::string, websocketpp::connection_hdl> websockets;
    static std::ostream os;
    static LogStream ls;

    static bool on_validate(websocketpp::connection_hdl hdl);
    static void on_fail(websocketpp::connection_hdl hdl);
    static void on_close(websocketpp::connection_hdl hdl);
    
};
