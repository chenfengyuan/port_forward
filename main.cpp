#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/thread.hpp>
#include <mutex>
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
        if(argc < 5){
            std::cout << "Usage: " << argv[0] << " listen_host listen_port destination_host destination_port [threads]" << std::endl;
            return 1;
        }
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
        return 0;
    }catch(std::exception const &e){
        std::cerr << e.what() << std::endl;
        return 2;
    }
}

