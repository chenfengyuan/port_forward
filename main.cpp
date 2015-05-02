#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/thread.hpp>
#include <mutex>
#include <algorithm>
#include <iterator>

template<typename T>
struct CoroutineWrapper{
    T func_;
    CoroutineWrapper(T  && func):func_(std::move(func)){
    }
    void operator()(boost::asio::yield_context yield){
        try{
            return func_(yield);
        }
        catch(boost::system::system_error const & e){
            std::cerr << "CoroutineWrapper Exception: " << e.what() << "(" << e.code() << ")\n";
        }
    }
};
class Pipe : public std::enable_shared_from_this<Pipe>{
public:
    bool errorp{false};
    using socket = boost::asio::ip::tcp::socket;
    Pipe(socket && socket0, socket && socket1):socket_0(std::move(socket0)), socket_1(std::move(socket1)), strand_(socket_0.get_io_service()){
    }
    void start();
    socket socket_0, socket_1;
    boost::asio::strand strand_;
};
void Pipe::start(){
//    std::cerr << "start\n";
    auto self = shared_from_this();
    auto read_and_write = [self](boost::asio::yield_context yield, socket & socket_src, socket & socket_dst){
        std::vector<char> buf;
        buf.resize(4096);
        while(true){
            try{
                auto length = socket_src.async_read_some(boost::asio::buffer(buf), yield);
                boost::asio::async_write(socket_dst, boost::asio::buffer(buf.data(), length), yield);
            }catch(const boost::system::system_error &e){
//                std::cerr << "normal exit:" << e.what() << "\n";
                try{
                    socket_src.close();
                }catch(const boost::system::system_error & e){
//                    std::cerr << "failed to close src:" << e.what() << "\n";
                }
                try{
                    socket_dst.close();
                }catch(const boost::system::system_error& e){
//                    std::cerr << "failed to close dst:" << e.what() << "\n";
                }

                break;
            }

        }
    };
    boost::asio::spawn(strand_, [this, read_and_write](boost::asio::yield_context yield){
        read_and_write(yield, socket_0, socket_1);
    });
    boost::asio::spawn(strand_, [this, read_and_write](boost::asio::yield_context yield){
        read_and_write(yield, socket_1, socket_0);
    });
}

boost::asio::ip::tcp::socket async_connect(boost::asio::io_service &io, boost::asio::yield_context yield, std::string host, std::string port){
    boost::asio::ip::tcp::socket socket(io);
    boost::asio::ip::tcp::resolver::query query_(host, port);
    boost::asio::ip::tcp::resolver resolver_(io);
    auto iter = resolver_.async_resolve(query_, yield);
    boost::asio::async_connect(socket, iter, boost::asio::ip::tcp::resolver::iterator{}, yield);
    return socket;
}

class SOCKS5Server
{
public:
    SOCKS5Server(boost::asio::io_service& io_service, std::string const & host,std::string const & port)
        : acceptor_(io_service), socket_(io_service)
    {
        try{
            boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(host), std::atoi(port.c_str()));
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            acceptor_.bind(endpoint);
            acceptor_.listen();
        }catch(std::exception const &e){
            std::cerr << "ERROR : Failed to bind address" << std::endl;
            std::cerr << e.what() << std::endl;
        }

        do_accept();
    }

    unsigned short get_port(void){
        return acceptor_.local_endpoint().port();
    }

private:
    void do_accept()
    {
        acceptor_.async_accept(socket_,
                               [this](boost::system::error_code ec)
        {
            if (!ec)
            {
                auto & io= socket_.get_io_service();
                auto func = [socket = std::move(socket_)](boost::asio::yield_context yield) mutable{
                    {
                        char buf[2];
                        boost::asio::async_read(socket, boost::asio::buffer(buf), yield);
                        if(buf[0] != 5){
                            std::cerr << "ERROR: Only Support SOCKS5\n";
                            return;
                        }
                        int n = buf[1];
                        std::vector<char> methods(n);
                        boost::asio::async_read(socket, boost::asio::buffer(methods), yield);
                        if(std::find(std::begin(methods), std::end(methods), '\x00') == std::end(methods)){
                            char buf[] = {'\x05', '\xff'};
                            boost::asio::async_write(socket,boost::asio::buffer(buf), yield);
                            std::cerr << "ERROR: only support \"No authentication\" authentication\n";
                            return;
                        }else{
                            char buf[] = {'\x05', '\x00'};
                            boost::asio::async_write(socket, boost::asio::buffer(buf), yield);
                        }
                    }
                    unsigned char buf[32];
                    socket.async_read_some(boost::asio::buffer(buf, 4), yield);
                    if (buf[0] != 0x05 || buf[1] != 0x01){
                        char buf[] = {0x05, 0x08};
                        boost::asio::async_write(socket, boost::asio::buffer(buf), yield);
                        return;
                    }
                    if (buf[3] == 0x01){
                        boost::asio::ip::address_v4::bytes_type ip_buf;
                        boost::asio::async_read(socket, boost::asio::buffer(ip_buf), yield);
                        boost::asio::ip::address_v4 dst_ip(ip_buf);
                        unsigned char port_buf[2];
                        boost::asio::async_read(socket, boost::asio::buffer(port_buf), yield);
                        int port = (port_buf[0] << 8) + port_buf[1];
                        boost::asio::ip::tcp::socket dst_socket = async_connect(socket.get_io_service(), yield, dst_ip.to_string(), std::to_string(port));
                        unsigned char buf_[] = {0x05, 0x00, 0x00, 0x01, ip_buf[0], ip_buf[1], ip_buf[2], ip_buf[3], port_buf[0], port_buf[1]};
                        boost::asio::async_write(socket, boost::asio::buffer(buf_), yield);
                        std::make_shared<Pipe>(std::move(socket), std::move(dst_socket))->start();
                    }else if(buf[3] == 0x03){
                        unsigned char len_buf[1];
                        boost::asio::async_read(socket, boost::asio::buffer(len_buf), yield);
                        int len = static_cast<int>(len_buf[0]);
                        char address[255];
                        boost::asio::async_read(socket, boost::asio::buffer(address, len + 2), yield);
                        std::string host(std::begin(address), std::begin(address) + len);
                        int port = (address[len] << 8) + address[len + 1];
                        boost::asio::ip::tcp::socket dst_socket = async_connect(socket.get_io_service(), yield, host, std::to_string(port));
                        unsigned char buf[] = {0x05, 0x00, 0x00, 0x03, len_buf[0]};
                        boost::asio::async_write(socket, boost::asio::buffer(buf), yield);
                        boost::asio::async_write(socket, boost::asio::buffer(address, len + 2), yield);
                        std::make_shared<Pipe>(std::move(socket), std::move(dst_socket))->start();
                    }else{
                        unsigned char buf[] = {0x05, 0x08};
                        boost::asio::async_write(socket, boost::asio::buffer(buf), yield);
                    }
                };
                boost::asio::spawn(io, CoroutineWrapper<decltype(func)>(std::move(func)));
            }

            do_accept();
        });
    }

    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ip::tcp::socket socket_;
};

class AcceptServer
{
public:
    AcceptServer(boost::asio::io_service& io_service, std::string const & host,std::string const & port,std::string const & dst_host, std::string const & dst_port)
        : acceptor_(io_service), socket_(io_service), dst_host_(dst_host), dst_port_(dst_port)
    {
        try{
            boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(host), std::atoi(port.c_str()));
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            acceptor_.bind(endpoint);
            acceptor_.listen();
        }catch(std::exception const &e){
            std::cerr << "ERROR : Failed to bind address" << std::endl;
            std::cerr << e.what() << std::endl;
        }

        do_accept();
    }

    unsigned short get_port(void){
        return acceptor_.local_endpoint().port();
    }

private:
    void do_accept()
    {
        acceptor_.async_accept(socket_,
                               [this](boost::system::error_code ec)
        {
            if (!ec)
            {
                auto & io= socket_.get_io_service();
                auto func = [socket = std::move(socket_), dst_host=dst_host_, dst_port=dst_port_](boost::asio::yield_context yield) mutable{
                    auto socket_dst = async_connect(socket.get_io_service(), yield, dst_host, dst_port);
                    std::make_shared<Pipe>(std::move(socket), std::move(socket_dst))->start();
                };
                boost::asio::spawn(io, CoroutineWrapper<decltype(func)>(std::move(func)));
            }

            do_accept();
        });
    }

    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ip::tcp::socket socket_;
    std::string dst_host_;
    std::string dst_port_;
};

int main(int argc, char *argv[])
{
    try{
        if(argc >= 5){
            auto listen_host = argv[1];
            auto listen_port = argv[2];
            auto dst_host = argv[3];
            auto dst_port = argv[4];
            auto num_of_threads = 1;
            if(argc == 6){
                num_of_threads = std::atoi(argv[5]);
            }
            num_of_threads--;

            using thread_ptr = std::shared_ptr<boost::thread>;
            std::vector<thread_ptr> threads;

            boost::asio::io_service io_service_;
            AcceptServer s(io_service_, listen_host, listen_port, dst_host, dst_port);
            for(auto i = 0; i< num_of_threads;++i){
                thread_ptr thread(new boost::thread(boost::bind(&boost::asio::io_service::run, &io_service_)));
                threads.push_back(thread);
            }
            for(auto i = 0;i<num_of_threads;++i){
                threads[i]->join();
            }
            io_service_.run();
        }else if(argc == 3){
            auto listen_host = argv[1];
            auto listen_port = argv[2];

            boost::asio::io_service io_service_;
            SOCKS5Server s(io_service_, listen_host, listen_port);
            io_service_.run();
        }else{
            std::cout << "Usage: " << argv[0] << " listen_host listen_port [destination_host destination_port [threads]]" << std::endl;
            return 1;
        }
        return 0;
    }catch(std::exception const &e){
        std::cerr << e.what() << std::endl;
        return 2;
    }
}

