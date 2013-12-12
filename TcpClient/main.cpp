#include "boost\asio\error.hpp"
#include "TcpClient.h"
#include <ctime>
#include <vector>
using namespace boost::asio;

class TcpClientApp{
public:
    TcpClientApp():m_bConnected(false)
    {

    }
    void setup()
    {
        mTcpClientRef = TcpClientRef( new TcpClient() );

        // listen to the TcpClient's signals
        mTcpClientRef->ConnectedCallback    =  boost::bind( &TcpClientApp::onConnected, this, boost::arg<1>::arg() ) ;
        mTcpClientRef->DisconnectedCallback =  boost::bind( &TcpClientApp::onDisconnected, this, boost::arg<1>::arg() ) ;
        mTcpClientRef->MessageCallback      =  boost::bind( &TcpClientApp::onMessage, this, boost::arg<1>::arg() ) ;
        mTcpClientRef->ExceptionCallback    =  std::tr1::bind( &TcpClientApp::onException, this,std::tr1::placeholders::_1);

        // connect to the Lycos mail server (as a test)
        mTcpClientRef->setDelimiter("\r\n");
        mTcpClientRef->connect("10.104.2.30", 1000);
    }

    // slots that are called by the server
    void onConnected(const boost::asio::ip::tcp::endpoint&)
    {
        std::cout<< "onConnected " << std::endl;
        m_bConnected = true;
    }
    void onDisconnected(const boost::asio::ip::tcp::endpoint&)
    {
        std::cout<< "onDisconnected " << std::endl;
        m_bConnected = false;
    }
    void onMessage(const std::string& msg)
    {
        std::cout<< "recv server msg is " << msg << std::endl;
    }

    void sendMessage(const char* msg)
    {
        mTcpClientRef->write(msg);
    }

    void update()
    {
        mTcpClientRef->update();
    }

    void onException(const boost::system::error_code& error)
    {
        std::cout<< "onException  msg is " << error.value() << " " <<error.message() << std::endl;
    }

    bool isConnected(){return m_bConnected;}
protected:
    //! our Boost ASIO TCP Client
    TcpClientRef	mTcpClientRef;
    bool m_bConnected;
};

void client()
{
    TcpClientApp tcapp;
    tcapp.setup();

    std::string cmd;
    while (1)
    {
        tcapp.update();
        Sleep(10);
    }

}

using boost::asio::ip::tcp;

std::string make_daytime_string()
{
    using namespace std; // For time_t, time and ctime;
    time_t now = time(0);
    return ctime(&now);
}
void server()
{
    try
    {
        boost::asio::io_service io_service;

        tcp::acceptor acceptor(io_service, tcp::endpoint(tcp::v4(), 1000));


            tcp::socket socket(io_service);
            acceptor.accept(socket);
            std::cout << " Client connected " << socket.remote_endpoint().address() << std::endl;
            
            boost::system::error_code ignored_error;
            while(true)
            {
                std::string message = make_daytime_string();
                boost::asio::write(socket, boost::asio::buffer(message + "\r\n"), ignored_error);
                io_service.poll(ignored_error);
                Sleep(1000);
            }

        }

    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }
}


int main()
{
    //server();
    client();
    system("pause");
    return 0;
}

