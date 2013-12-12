/****************************************************************************
 Copyright (c) 2013 libo

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/

#include <iostream>
#include "TcpClient.h"

TcpClient::TcpClient():
    m_isConnected(false), 
    m_isClosing(false),
	m_socket(m_ioServer), 
    m_heartBeatTimer(m_ioServer),
    m_reconnectTimer(m_ioServer),
    m_heartBeat("PING"),
    m_heartBeatTimeOut(5),
    m_reconnectTimeOut(5)
{	
	// for servers that terminate their messages with a null-byte
	m_delimiter = "\0"; 
}

TcpClient::TcpClient( const std::string &heartbeat ):
    m_isConnected(false),
    m_isClosing(false),
	m_socket(m_ioServer),
    m_heartBeatTimer(m_ioServer), 
    m_reconnectTimer(m_ioServer), 
    m_heartBeat(heartbeat),
    m_heartBeatTimeOut(5),
    m_reconnectTimeOut(5)
{	
	// for servers that terminate their messages with a null-byte
	m_delimiter = "\0"; 
}

TcpClient::~TcpClient(void)
{
	disconnect();
}

void TcpClient::update()
{
	// calls the poll() function to process network messages
	m_ioServer.poll();
}

void TcpClient::connect(const std::string &ip, unsigned short port)
{
	// connect socket
	try 
    {
		boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(ip), port);
		connect(endpoint);
	}
	catch(const std::exception &e)
    {
		std::cout << "Server exception:" << e.what() << std::endl;
	}
}

void TcpClient::connect(const std::string &url, const std::string &protocol)
{
	// On Windows, see './Windows/system32/drivers/etc/services' for a list of supported protocols.
	//  You can also explicitly pass a port, like "8080"
	boost::asio::ip::tcp::resolver::query query( url, protocol );
	boost::asio::ip::tcp::resolver resolver( m_ioServer );

	try 
    {
		boost::asio::ip::tcp::resolver::iterator destination = resolver.resolve(query);

		boost::asio::ip::tcp::endpoint endpoint;
		while ( destination != boost::asio::ip::tcp::resolver::iterator() ) 
        {
			endpoint = *destination++;
        }
		connect(endpoint);
	}
	catch(const std::exception &e)
    {
		std::cout << "Server exception:" << e.what() << std::endl;
	}
}

void TcpClient::connect(boost::asio::ip::tcp::endpoint& endpoint)
{
	if(m_isConnected) return;
	if(m_isClosing) return;

	m_endPoint = endpoint;

	//std::cout << "Trying to connect to port " << endpoint.port() << " @ " << endpoint.address().to_string() << std::endl;

	// try to connect, then call handle_connect
	m_socket.async_connect(endpoint,
        boost::bind(&TcpClient::handle_connect, this, boost::asio::placeholders::error));
}

void TcpClient::disconnect()
{		
	// tell socket to close the connection
	close();
	
	// tell the IO service to stop
	m_ioServer.stop();

	m_isConnected = false;
}

void TcpClient::write(const std::string &msg)
{
	if(!m_isConnected) return;
	if(m_isClosing) return;

	// safe way to request the client to write a message
	m_ioServer.post(boost::bind(&TcpClient::do_write, this, msg));
}

void TcpClient::close()
{
	if(!m_isConnected) return;

	// safe way to request the client to close the connection
	m_ioServer.post(boost::bind(&TcpClient::do_close, this));
}

void TcpClient::read()
{
	if(!m_isConnected) return;
	if(m_isClosing) return;

	// wait for a message to arrive, then call handle_read
    boost::asio::async_read_until(m_socket, m_buffer, m_delimiter,
        boost::bind(&TcpClient::handle_read, this, boost::asio::placeholders::error));

}

// callbacks

void TcpClient::handle_connect(const boost::system::error_code& error) 
{
	if(m_isClosing) return;
	
	if (!error) 
    {
		// we are connected!
		m_isConnected = true;

		// let listeners know
		ConnectedCallback(m_endPoint);

		// start heartbeat timer (optional)	
		m_heartBeatTimer.expires_from_now(boost::posix_time::seconds(m_heartBeatTimeOut));
		m_heartBeatTimer.async_wait(boost::bind(&TcpClient::do_heartbeat, this, boost::asio::placeholders::error));

		// await the first message
		read();
	}
	else
    {
		// there was an error :(
		m_isConnected = false;

        ExceptionCallback(error);

		// schedule a timer to reconnect after 5 seconds		
		m_reconnectTimer.expires_from_now(boost::posix_time::seconds(m_reconnectTimeOut));
		m_reconnectTimer.async_wait(boost::bind(&TcpClient::do_reconnect, this, boost::asio::placeholders::error));
	}
}

void TcpClient::handle_read(const boost::system::error_code& error)
{
	if (!error)
	{
		std::string msg;
		std::istream is(&m_buffer);
		std::getline(is, msg); 
		
		if(msg.empty()) return;

		// TODO: you could do some message processing here, like breaking it up
		//       into smaller parts, rejecting unknown messages or handling the message protocol

		// create function to notify listeners
		MessageCallback(msg);

		// restart heartbeat timer (optional)	
		m_heartBeatTimer.expires_from_now(boost::posix_time::seconds(m_heartBeatTimeOut));
		m_heartBeatTimer.async_wait(boost::bind(&TcpClient::do_heartbeat, this, boost::asio::placeholders::error));

		// wait for the next message
		read();
	}
	else
	{
        ExceptionCallback(error);

		// try to reconnect if external host disconnects
		if(error.value() != 0) 
        {
			m_isConnected = false;

			// let listeners know
			DisconnectedCallback(m_endPoint); 
			
			// schedule a timer to reconnect after 5 seconds
			m_reconnectTimer.expires_from_now(boost::posix_time::seconds(m_reconnectTimeOut));
			m_reconnectTimer.async_wait(boost::bind(&TcpClient::do_reconnect, this, boost::asio::placeholders::error));
		}
        else
        {
            do_close();
        }	
	}
}

void TcpClient::handle_write(const boost::system::error_code& error)
{
	if(!error && !m_isClosing)
	{
		// write next message
		m_messages.pop_front();
		if (!m_messages.empty())
		{
			//ci::app::console() << "Client message:" << m_messages.front() << std::endl;

			boost::asio::async_write(m_socket,
				boost::asio::buffer(m_messages.front()),
				boost::bind(&TcpClient::handle_write, this, boost::asio::placeholders::error));
		}
		else {
			// restart heartbeat timer (optional)	
			m_heartBeatTimer.expires_from_now(boost::posix_time::seconds(m_heartBeatTimeOut));
			m_heartBeatTimer.async_wait(boost::bind(&TcpClient::do_heartbeat, this, boost::asio::placeholders::error));
		}
	}
    else if (error)
    {
        ExceptionCallback(error);
    }
}

void TcpClient::do_write(const std::string &msg)
{
	if(!m_isConnected) return;

	bool write_in_progress = !m_messages.empty();
	m_messages.push_back(msg + m_delimiter);

	if (!write_in_progress && !m_isClosing)
	{
		boost::asio::async_write(m_socket,
			boost::asio::buffer(m_messages.front()),
			boost::bind(&TcpClient::handle_write, this, boost::asio::placeholders::error));
	}
}

void TcpClient::do_close()
{
	if(m_isClosing) return;
	
	m_isClosing = true;

	m_socket.close();
}

void TcpClient::do_reconnect(const boost::system::error_code& error)
{
	if(m_isConnected) return;
	if(m_isClosing) return;

	// close current socket if necessary
	m_socket.close();

	// try to reconnect, then call handle_connect
	m_socket.async_connect(m_endPoint,
        boost::bind(&TcpClient::handle_connect, this, boost::asio::placeholders::error));
}

void TcpClient::do_heartbeat(const boost::system::error_code& error)
{
	// here you can regularly send a message to the server to keep the connection alive,
	// I usualy send a PING and then the server replies with a PONG

	if(!error) write( m_heartBeat );
}

