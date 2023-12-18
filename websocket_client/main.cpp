#include <iostream>
#include <string>
#include <map>
#include <sstream>
#include <cstdlib>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/common/thread.hpp>
#include <websocketpp/common/memory.hpp>
 
using namespace std;



typedef websocketpp::client<websocketpp::config::asio_client> client;

class connection_metadata {
public:
    typedef websocketpp::lib::shared_ptr<connection_metadata> ptr;
    connection_metadata(int id, websocketpp::connection_hdl hdl, string uri): 
        m_id(id),
        m_hdl(hdl),
        m_status("connecting"),
        m_uri(uri),
        m_server("N/A")
    {}

    void on_open(client * c, websocketpp::connection_hdl hdl) {
        m_status = "Open";
        client::connection_ptr con = c->get_con_from_hdl(hdl);
        m_server = con->get_response_header("Server");
    }

    void on_fail(client * c, websocketpp::connection_hdl hdl) {
        m_status = "Failed";
        client::connection_ptr con = c->get_con_from_hdl(hdl);
        m_server = con->get_response_header("Server");
        m_error_reason = con->get_ec().message();
    }
    friend ostream & operator << (ostream & out, connection_metadata const &data);

    void on_close(client * c, websocketpp::connection_hdl hdl) {
        m_status = "Closed";
        client::connection_ptr con = c->get_con_from_hdl(hdl);
        std::stringstream s;
        s << "close code: " << con->get_remote_close_code() << " (" 
        << websocketpp::close::status::get_string(con->get_remote_close_code()) 
        << "), close reason: " << con->get_remote_close_reason();
        m_error_reason = s.str();
    }

    void on_message(websocketpp::connection_hdl hdl, client::message_ptr msg) {
        if (msg->get_opcode() == websocketpp::frame::opcode::text) {
            m_messages.push_back(msg->get_payload());
        } else {
            m_messages.push_back(websocketpp::utility::to_hex(msg->get_payload()));
        }
    }

    void record_sent_message(std::string message) {
        m_messages.push_back(">> " + message);
    }

    
    websocketpp::connection_hdl get_hdl() {
        return m_hdl;
    }
    string get_status() {
        return m_status;
    }
    int get_id() {
        return m_id;
    }
private:
    int m_id;
    websocketpp::connection_hdl m_hdl;
    string m_status;
    string m_uri;
    string m_server;
    string m_error_reason;
    vector<std::string> m_messages;
};


std::ostream & operator<< (std::ostream & out, connection_metadata const & data) {
    out << "> URI: " << data.m_uri << "\n"
        << "> Status: " << data.m_status << "\n"
        << "> Remote Server: " << (data.m_server.empty() ? "None Specified" : data.m_server) << "\n"
        << "> Error/close reason: " << (data.m_error_reason.empty() ? "N/A" : data.m_error_reason);
 
    return out;
}



class websocket_endpoint {
public:
    websocket_endpoint() {
        // 日志通道clear
        m_endpoint.clear_access_channels(websocketpp::log::alevel::all); //访问日志
        m_endpoint.clear_error_channels(websocketpp::log::elevel::all); //错误日志

        m_endpoint.init_asio(); //网络操作用的库
        m_endpoint.start_perpetual(); //永久运行模式


        // reset方法用于改变m_thread指向的对象 -> 一个新的thread -> 里面有thread函数和参数。
        m_thread.reset(new websocketpp::lib::thread(&client::run, &m_endpoint)); //创建并启动thread
    }
    int connect(string const &uri) {
        websocketpp::lib::error_code ec;
        client::connection_ptr con = m_endpoint.get_connection(uri, ec);
        if (ec) {
            return -1;
        };
        int new_id = m_next_id++;
        connection_metadata::ptr metadata_ptr(new connection_metadata(new_id, con->get_handle(), uri));
        m_connection_list[new_id] = metadata_ptr;

        con->set_open_handler(websocketpp::lib::bind(
            &connection_metadata::on_fail,
            metadata_ptr,
            &m_endpoint,
            websocketpp::lib::placeholders::_1
        ));

        con->set_message_handler(websocketpp::lib::bind(
            &connection_metadata::on_message,
            metadata_ptr,
            websocketpp::lib::placeholders::_1,
            websocketpp::lib::placeholders::_2
        ));

        m_endpoint.connect(con);
        return new_id;
    }
    connection_metadata::ptr get_metadata(int id) const {
        con_list::const_iterator metadata_it = m_connection_list.find(id);
        if (metadata_it == m_connection_list.end()) {
            return connection_metadata::ptr();
        } else {
            return metadata_it->second;
        }
    }

    void close(int id, websocketpp::close::status::value code) {
        websocketpp::lib::error_code ec;
        
        con_list::iterator metadata_it = m_connection_list.find(id);
        if (metadata_it == m_connection_list.end()) {
            std::cout << "> No connection found with id " << id << std::endl;
            return;
        }
        
        m_endpoint.close(metadata_it->second->get_hdl(), code, "", ec);
        if (ec) {
            std::cout << "> Error initiating close: " << ec.message() << std::endl;
        }
    }


    void send(int id, std::string message) {
        websocketpp::lib::error_code ec;
        
        con_list::iterator metadata_it = m_connection_list.find(id);
        if (metadata_it == m_connection_list.end()) {
            std::cout << "> No connection found with id " << id << std::endl;
            return;
        }
        
        m_endpoint.send(metadata_it->second->get_hdl(), message, websocketpp::frame::opcode::text, ec);
        if (ec) {
            std::cout << "> Error sending message: " << ec.message() << std::endl;
            return;
        }
        
        metadata_it->second->record_sent_message(message);
    }


    ~websocket_endpoint() {
        m_endpoint.stop_perpetual();
        
        for (con_list::const_iterator it = m_connection_list.begin(); it != m_connection_list.end(); ++it) {
            if (it->second->get_status() != "Open") {
                // Only close open connections
                continue;
            }
            
            std::cout << "> Closing connection " << it->second->get_id() << std::endl;
            
            websocketpp::lib::error_code ec;
            m_endpoint.close(it->second->get_hdl(), websocketpp::close::status::going_away, "", ec);
            if (ec) {
                std::cout << "> Error closing connection " << it->second->get_id() << ": "  
                        << ec.message() << std::endl;
            }
        }
        
        m_thread->join();
    }
private:
    typedef map<int, connection_metadata::ptr> con_list;
    client m_endpoint;
    websocketpp::lib::shared_ptr<websocketpp::lib::thread> m_thread; // 属于share_ptr，当没有ref指向它管理的对象时被销毁。
    con_list m_connection_list;
    int m_next_id;
};



int main() {
    bool done = false;
    string input;
    websocket_endpoint endpoint;

    
    while (!done) {
        cout << "Enter command:";
        getline(cin, input);

        if (input == "quit") {
            done = true;
        } else if (input == "help" ) {
            cout << "\n command list: \n"
                 << "help: display this help \n"
                 << "quit: exit the program \n" << endl;
        } else if (input.substr(0,7) == "connect") {
            int id = endpoint.connect(input.substr(8));
            if (id != -1) {
                cout << "> created with id " << id << endl;
            }
        } else if (input.substr(0,4) == "show") {
            int id = atoi(input.substr(5).c_str());
            connection_metadata::ptr metadata = endpoint.get_metadata(id);
            if (metadata) {
                cout << *metadata << endl;
            } else {
                cout << "unkown connect id " << id << endl;
            }
        } else if (input.substr(0,5) == "close") {
            std::stringstream ss(input);
            
            std::string cmd;
            int id;
            int close_code = websocketpp::close::status::normal;
            std::string reason;
            
            ss >> cmd >> id >> close_code;
            std::getline(ss,reason);
            
            endpoint.close(id, close_code);
        } else if (input.substr(0,4) == "send") {
            std::stringstream ss(input);
                
                std::string cmd;
                int id;
                std::string message = "";
                
                ss >> cmd >> id;
                std::getline(ss,message);
                
                endpoint.send(id, message);
        } else { 
            cout << "unrecognized command" << endl;
        }
    }
    return 0;

}