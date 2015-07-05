/*

Copyright (c) 2015, Arvid Norberg
All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "simulator/simulator.hpp"
#include <boost/bind.hpp>

using namespace sim::asio;
using namespace sim::chrono;
using sim::simulation;

void incoming_connection(boost::system::error_code const& ec
	, ip::tcp::socket& sock, ip::tcp::acceptor& listener)
{
	int millis = int(duration_cast<milliseconds>(high_resolution_clock::now()
		.time_since_epoch()).count());
	if (ec)
	{
		printf("[%4d] error while accepting connection: %s\n"
			, millis, ec.message().c_str());
		return;
	}

	printf("[%4d] received incoming connection\n", millis);
	sock.close();

	listener.async_accept(sock, boost::bind(&incoming_connection
		, _1, boost::ref(sock), boost::ref(listener)));
}

int counter = 0;

void on_connected(boost::system::error_code const& ec
	, ip::tcp::socket& sock)
{
	int millis = int(duration_cast<milliseconds>(high_resolution_clock::now()
		.time_since_epoch()).count());
	if (ec)
	{
		printf("[%4d] error while connecting: %s\n", millis, ec.message().c_str());
		return;
	}

	printf("[%4d] made outgoing connection\n", millis);
	sock.close();

	if (++counter > 5) return;

	sock.async_connect(ip::tcp::endpoint(ip::address::from_string("40.30.20.10")
		, 1337), boost::bind(&on_connected, _1, boost::ref(sock)));
}

int main()
{
	simulation sim;
	io_service incoming_ios(sim, ip::address_v4::from_string("40.30.20.10"));
	io_service outgoing_ios(sim, ip::address_v4::from_string("10.20.30.40"));
	ip::tcp::acceptor listener(incoming_ios);

	int millis = int(duration_cast<milliseconds>(high_resolution_clock::now()
		.time_since_epoch()).count());

	boost::system::error_code ec;
	listener.open(ip::tcp::v4(), ec);
	if (ec) printf("[%4d] open failed: %s\n", millis, ec.message().c_str());
	listener.bind(ip::tcp::endpoint(ip::address(), 1337), ec);
	if (ec) printf("[%4d] bind failed: %s\n", millis, ec.message().c_str());
	listener.listen(10, ec);
	if (ec) printf("[%4d] listen failed: %s\n", millis, ec.message().c_str());

	ip::tcp::socket incoming(incoming_ios);
	listener.async_accept(incoming
		, boost::bind(&incoming_connection, _1, boost::ref(incoming)
		, boost::ref(listener)));

	printf("[%4d] connecting\n", millis);
	ip::tcp::socket outgoing(outgoing_ios);
	outgoing.open(ip::tcp::v4(), ec);
	if (ec) printf("[%4d] open failed: %s\n", millis, ec.message().c_str());
	outgoing.async_connect(ip::tcp::endpoint(ip::address::from_string("40.30.20.10")
		, 1337), boost::bind(&on_connected, _1, boost::ref(outgoing)));

	sim.run(ec);

	millis = int(duration_cast<milliseconds>(high_resolution_clock::now()
		.time_since_epoch()).count());

	printf("[%4d] simulation::run() returned: %s\n"
		, millis, ec.message().c_str());
}

