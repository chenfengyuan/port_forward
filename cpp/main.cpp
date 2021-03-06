#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/thread.hpp>
#include <mutex>
#if PORT_FORWARD_ENABLE_STACK_TRACE
#include "stacktrace.h"
#endif
#include <algorithm>
#include <iterator>
#include <boost/regex.hpp>
#include <sstream>
#include <unordered_map>
#include <chrono>

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
#if PORT_FORWARD_ENABLE_STACK_TRACE
            print_stacktrace();
#endif
            std::cerr << "CoroutineWrapper Exception: " << e.what() << "(" << e.code() << ")\n";
        }
    }
};

class RedirectTrace{
    std::vector<std::unordered_map<std::string, std::string>> dicts{};
    std::chrono::time_point<std::chrono::system_clock> next_rotate_and_clear_point{std::chrono::system_clock::now()};
    std::chrono::seconds duration{60};
    std::mutex mutex;

    void rotate_and_clear(){
        if(std::chrono::system_clock::now() > next_rotate_and_clear_point){
            dicts[0].clear();
            std::rotate(std::begin(dicts), std::begin(dicts) + 1, std::end(dicts));
            next_rotate_and_clear_point += duration;
        }
    }
public:
    RedirectTrace(){
        dicts.resize(2);
    }
    void set(std::string const & key, std::string const & value){
        std::lock_guard<std::mutex> lock(mutex);
        dicts[1][key] = value;
        rotate_and_clear();
    }
    std::string get(std::string const & key){
        std::lock_guard<std::mutex> lock(mutex);
        auto iter = dicts[1].find(key);
        if(iter != dicts[1].end()){
            return dicts[1][key];
        }
        iter = dicts[0].find(key);
        if(iter != dicts[0].end()){
            return dicts[0][key];
        }
        rotate_and_clear();
        return "";
    }
};


std::string search(boost::regex const & pattern, boost::asio::streambuf const & buf){
    typedef typename boost::asio::streambuf::const_buffers_type const_buffers_type;
    typedef boost::asio::buffers_iterator<const_buffers_type> iterator;
    const_buffers_type buffers = buf.data();
    iterator begin = iterator::begin(buffers);
    iterator end = iterator::end(buffers);
    boost::match_results<iterator> match_results;
    boost::regex_search(begin, end, match_results, pattern);
    if(! match_results[0].matched)
        return "";
    else
        return match_results[0].str();
}

std::string encode(std::string const & src){
    std::ostringstream t(std::ios::out | std::ios::binary);
    t << "'";
    std::ostream_iterator<char, char> oi(t);
    boost::regex_replace(oi, src.begin(), src.end(), boost::regex("'"), R"a('"'"')a", boost::match_default | boost::format_all);
    t << "'";
    return t.str();
}

std::string get_request_path(std::string const &in){
    boost::regex pattern("^GET +(/[^ ]+)", boost::regex::perl);
    boost::match_results<std::string::const_iterator> match_results;
    boost::regex_search(in.begin(), in.end(), match_results, pattern);
    auto url = match_results[1].str();
    return url;
}

std::string get_302_location_path(boost::asio::streambuf const & buf){
    boost::regex pattern{"HTTP/\\d\\.\\d +?302.+\r\nLocation: +http://[^/]+([^ \r\n]+)", boost::regex::perl};
    typedef typename boost::asio::streambuf::const_buffers_type const_buffers_type;
    typedef boost::asio::buffers_iterator<const_buffers_type> iterator;
    const_buffers_type buffers = buf.data();
    iterator begin = iterator::begin(buffers);
    iterator end = iterator::end(buffers);
    boost::match_results<iterator> match_results;
    boost::regex_search(begin, end, match_results, pattern);
    if(match_results[0].matched){
        return match_results[1].str();
    }
    return "";
}


std::string convert_request_to_curl_cmd(std::string const & in){
    std::ostringstream out;
    boost::regex pattern("^GET +(/[^ ]+)", boost::regex::perl);
    boost::match_results<std::string::const_iterator> match_results;
    boost::regex_search(in.cbegin(), in.cend(), match_results, pattern);
    auto url = match_results[1].str();
    pattern = "Host: +([^ \r\n]+)";
    boost::regex_search(in.cbegin(), in.cend(), match_results, pattern);
    auto host = match_results[1].str();
    url = "http://" + host + url;
    out << "curl " << encode(url) << " ";
    pattern = boost::regex("^([^:\r\n]+): +(?:[^\r\n]+)$", boost::regex::perl);
    auto start = in.cbegin();
    auto end = in.cend();
    while(boost::regex_search(start, end, match_results, pattern)){
        if(match_results[0].matched){
            if(match_results[1].str() != "Host")
                out << "-H " << encode(match_results.str()) << " ";
            start += match_results.position() + match_results.length();
        }
    }
    out << "--compressed ";
    return out.str();
}

class RequestFilter{
    boost::asio::streambuf request_buf{};
    std::ostream request_os{&request_buf};
    boost::asio::streambuf response_buf{};
    std::ostream response_os{&response_buf};
    static const boost::regex request_pattern;
    static const boost::regex response_pattern;
    std::string request{};
    static RedirectTrace redirect_trace;
public:
    void add_request_content(std::vector<char> buf, std::size_t len){
        if(request_buf.size() > 4096 * 3)
            return;
        request_os.write(buf.data(), len);
        auto result = search(request_pattern, request_buf);
        if(result.length() > 0){
//            std::cout << result << std::endl;
//            auto path = get_request_path(result);
//            std::cout << path << std::endl;
            request = result;
            request_buf.consume(request_buf.size());
        }
//        if(request_buf.size() > 4096 * 3){
//            request_buf.consume(request_buf.size() - 4096 * 2);
//        }
    }
    void add_response_content(std::vector<char> buf, std::size_t len){
        if (response_buf.size()  > 4096 * 3)
            return;
        response_os.write(buf.data(), len);
        auto location_path = get_302_location_path(response_buf);
        if(location_path.length() > 0){
            if(request.length() > 0){
                redirect_trace.set(location_path, std::move(request));
                request = "";
            }
//            std::cout << location_path << std::endl;
            request_buf.consume(request_buf.size());
            response_buf.consume(response_buf.size());
            return;
        }
        auto result = search(response_pattern, response_buf);
        if(result.length() > 0){
            if(request.length() > 0){
                auto path = get_request_path(request);
                auto const & request_tmp = redirect_trace.get(path);
                if(request_tmp.length() > 0){
                    std::cout << convert_request_to_curl_cmd(request_tmp) << std::endl;
                } else{
                    std::cout << convert_request_to_curl_cmd(request) << std::endl;
                }
		std::cout << std::endl;
                request = "";
            }
            response_buf.consume(response_buf.size());
            request_buf.consume(request_buf.size());
        }
//        if(response_buf.size() > 4096 * 3){
//            response_buf.consume(response_buf.size() - 4096 * 2);
//        }
    }
};
const boost::regex RequestFilter::request_pattern{"GET /.*HTTP/\\d\\.\\d\r\n.*\r\n\r\n", boost::regex::perl};
const boost::regex RequestFilter::response_pattern{"Content-Disposition: +attachment; *filename=", boost::regex::perl};
RedirectTrace RequestFilter::redirect_trace{};

class Pipe : public std::enable_shared_from_this<Pipe>{
public:
    using socket = boost::asio::ip::tcp::socket;
    Pipe(socket && socket0, socket && socket1):socket_0(std::move(socket0)), socket_1(std::move(socket1)), strand_(socket_0.get_io_service()){
    }
    void start();
    socket socket_0, socket_1;
    boost::asio::strand strand_;
    RequestFilter filter{};
};
void Pipe::start(){
//    std::cerr << "start\n";
    auto self = shared_from_this();
    auto read_and_write = [self, this](boost::asio::yield_context yield, socket & socket_src, socket & socket_dst, bool request_part){
//        const boost::regex pattern("Content-Disposition: attachment;[^\r]+(?=\r\n)", boost::regex::perl);
//        const boost::regex pattern("GET /.*HTTP/\\d\\.\\d\r\n.*\r\n\r\n", boost::regex::perl);
        std::vector<char> buf;
        buf.resize(4096);
        while(true){
            try{
                auto length = socket_src.async_read_some(boost::asio::buffer(buf), yield);
                if(request_part){
                    filter.add_request_content(buf, length);
                }else{
                    filter.add_response_content(buf, length);
                }
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
        read_and_write(yield, socket_0, socket_1, true);
    });
    boost::asio::spawn(strand_, [this, read_and_write](boost::asio::yield_context yield){
        read_and_write(yield, socket_1, socket_0, false);
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

void output_char_array(std::basic_ostream<char> &out, unsigned char * arr, int len){
    for(int i=0;i<len;++i){
        out << static_cast<int>(arr[i]);
    }
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
//                        std::cerr << dst_ip.to_string() << " " << std::to_string(port) << "\n";
                        boost::asio::ip::tcp::socket dst_socket = async_connect(socket.get_io_service(), yield, dst_ip.to_string(), std::to_string(port));
//                        std::cerr << "connected\n";
                        unsigned char buf_[] = {0x05, 0x00, 0x00, 0x01, ip_buf[0], ip_buf[1], ip_buf[2], ip_buf[3], port_buf[0], port_buf[1]};
                        boost::asio::async_write(socket, boost::asio::buffer(buf_), yield);
                        std::make_shared<Pipe>(std::move(socket), std::move(dst_socket))->start();
                    }else if(buf[3] == 0x03){
                        unsigned char len_buf[1];
                        boost::asio::async_read(socket, boost::asio::buffer(len_buf), yield);
                        int len = static_cast<int>(len_buf[0]);
                        unsigned char address[255];
                        boost::asio::async_read(socket, boost::asio::buffer(address, len + 2), yield);
//                        output_char_array(std::cerr, address, 255);
                        std::string host(std::begin(address), std::begin(address) + len);
                        int port = (address[len] << 8) + address[len + 1];
//                        std::cerr << host << " " << std::to_string(port) << "\n";
                        boost::asio::ip::tcp::socket dst_socket = async_connect(socket.get_io_service(), yield, host, std::to_string(port));
//                        std::cerr << "connected\n";
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
            do_accept();
        }catch(std::exception const &e){
#if PORT_FORWARD_ENABLE_STACK_TRACE
            print_stacktrace();
            std::cerr << "ERROR : Failed to bind address" << std::endl;
            throw;
#else
            std::cerr << "ERROR : Failed to bind address" << std::endl;
            std::cerr << e.what() << std::endl;
#endif
        }
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

