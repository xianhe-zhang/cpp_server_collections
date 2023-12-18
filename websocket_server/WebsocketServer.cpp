#include "websocketpp/server.hpp"
#include "websocketpp/config/asio_no_tls.hpp"
#include <string>
#include <map>
#include <ostream>
#include "./LogStream.hpp"
// 利用namespace可以省略代码前缀，risky
using namespace std;
using namespace websocketpp;

class WebsocketServer {

    websocketpp::server<websocketpp::config::asio> WebsocketServer::server;
    std::map<std::string, websocketpp::connection_hdl> WebsocketServer::websockets;
    pthread_rwlock_t WebsocketServer::websocketsLock = PTHREAD_RWLOCK_INITIALIZER;
    LogStream WebsocketServer::ls;
    std::ostream os;

    bool WebsocketServer::init(){
        server.init_asio();
        server.get_alog().set_ostream(&os);
        server.get_elog().set_ostream(&os);
        server.set_validate_handler([this](websocketpp::connection_hdl hdl) {
            return this->on_validate(hdl);
        });
        server.set_fail_handler([this](websocketpp::connection_hdl hdl) {
            this->on_fail(hdl);
        });
        server.set_close_handler([this](websocketpp::connection_hdl hdl) {
            this->on_close(hdl);
        });

        int port = 8082;
        try {
            server.listen(port);
        } catch (websocketpp::exception const&e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }

        // starting websocket accept;
        websocketpp::lib::error_code ec;
        server.start_accept(ec);
        if (ec) {
            return false;
        }
        return true;
    };

    void WebsocketServer::run() {
        try {
            server.run();
        } catch (websocketpp::exception const&e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
    };


    void WebsocketServer::stop() {
        websocketpp::lib::error_code ec;
        server.stop_listening(ec);
        if (ec) {
            return;
        }
        std::string data  = "Terminating connection...";
        std::map<std::string, websocketpp::connection_hdl>::iterator it;
        for (it = websockets.begin(); it != websockets.end(); ++it) {
            websocketpp::lib::error_code ec;
            server.close(it->second, websocketpp::close::status::normal, data, ec);
            if (ec) {
                std::cerr << "Exception: " << ec << std::endl;;
            }
        }
        server.stop();
    };

    
    bool WebsocketServer::sendData(std::string id, std::string data) {
        websocketpp::connection_hdl hdl;
        if (!getWebsocket(id, hdl)) {
            // sending to non-existing hdl
            return false;
        }

        websocketpp::lib::error_code ec;
        server.send(hdl, data, websocketpp::frame::opcode::text, ec);
        if (ec) {
            // we met a error
            return false;
        } 
        return true;
    }
    bool WebsocketServer::sendClose(std::string id) {
        websocketpp::connection_hdl hdl;
        if (!getWebsocket(id, hdl)) {
            return false;
        };

        std::string data = "terminating...";
        websocketpp::lib::error_code ec;
        server.close(hdl, websocketpp::close::status::normal, data, ec);
        if(ec) {
            return false;
        }

        pthread_rwlock_rdlock(&websocketsLock);
        websockets.erase(id);
        pthread_rwlock_unlock(&websocketsLock);
        return true;


    }

    static bool getWebsocket(const std::string &id, websocketpp::connection_hdl &hdl);
    static pthread_rwlock_t websocketsLock;
    static std::map<std::string, websocketpp::connection_hdl> websockets;
    // static ostream os;
    // static LogStream ls;

    bool on_validate(websocketpp::connection_hdl hdl) {
        websocketpp::server<websocketpp::config::asio>::connection_ptr con = server.get_con_from_hdl(hdl);
        websocketpp::uri_ptr uri = con->get_uri();
        std::string query = uri->get_query();
        if (!query.empty()) {
            // backend operatin
            // to split the query parameter
        } else { return false;}

        if (pthread_rwlock_wrlock(&websocketsLock) != 0) {
            // failed to write-lock
        } 
        std::string id = "success";
        websockets.insert(std::pair<std::string, websocketpp::connection_hdl>(id, hdl));
        if (pthread_rwlock_wrlock(&websocketsLock)!=0) {
            // failed to unlock
        }
        return true;
    };
    
    void WebsocketServer::on_fail(websocketpp::connection_hdl hdl) {
        websocketpp::server<websocketpp::config::asio>::connection_ptr con = server.get_con_from_hdl(hdl);
        websocketpp::lib::error_code ec = con->get_ec();
        // Websocket connection attempt by client failed. Log reason using ec.message().
    }
   
   
    void WebsocketServer::on_close(websocketpp::connection_hdl hdl) {
    // Websocket connection closed.
    }
    
};
