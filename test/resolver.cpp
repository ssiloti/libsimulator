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
#include <functional>

#include "catch.hpp"

using namespace std::placeholders;
using namespace sim;

using asio::ip::address_v4;
using duration = chrono::high_resolution_clock::duration;
using chrono::duration_cast;
using chrono::milliseconds;

int num_lookups = 0;

void on_name_lookup(boost::system::error_code const& ec
	, asio::ip::tcp::resolver::iterator iter)
{
	++num_lookups;

	int millis = int(duration_cast<milliseconds>(chrono::high_resolution_clock::now()
		.time_since_epoch()).count());

	std::vector<address_v4> expect = {
		address_v4::from_string("1.2.3.4")
			, address_v4::from_string("1.2.3.5")
			, address_v4::from_string("1.2.3.6")
			, address_v4::from_string("1.2.3.7") };

	auto expect_it = expect.begin();

	while (iter != asio::ip::tcp::resolver::iterator())
	{
		assert(iter->endpoint().address() == *expect_it);
		assert(iter->endpoint().port() == 8080);

		++iter;
		++expect_it;
	}

	assert(expect_it == expect.end());
}

void on_failed_name_lookup(boost::system::error_code const& ec
	, asio::ip::tcp::resolver::iterator iter)
{
	++num_lookups;

	assert(ec == boost::system::error_code(asio::error::host_not_found));

	int millis = int(duration_cast<milliseconds>(chrono::high_resolution_clock::now()
		.time_since_epoch()).count());
}

struct sim_config : sim::default_config
{
	duration hostname_lookup(
		asio::ip::address const& requestor
		, std::string hostname
		, std::vector<asio::ip::address>& result
		, boost::system::error_code& ec)
	{
		if (hostname == "test.com")
		{
			result = {
				address_v4::from_string("1.2.3.4")
				, address_v4::from_string("1.2.3.5")
				, address_v4::from_string("1.2.3.6")
				, address_v4::from_string("1.2.3.7")
			};
			return duration_cast<duration>(chrono::milliseconds(50));
		}

		return default_config::hostname_lookup(requestor, hostname, result, ec);
	}
};

TEST_CASE("resolve multiple IPv4 addresses", "resolver") {
	sim_config cfg;
	simulation sim(cfg);

	chrono::high_resolution_clock::time_point start = chrono::high_resolution_clock::now();
	num_lookups = 0;

	asio::io_service ios(sim, address_v4::from_string("40.30.20.10"));

	asio::ip::tcp::resolver resolver(ios);
	asio::ip::tcp::resolver::query q("test.com", "8080");
	resolver.async_resolve(q, std::bind(&on_name_lookup, _1, _2));

	boost::system::error_code ec;
	sim.run(ec);

	int millis = int(duration_cast<milliseconds>(chrono::high_resolution_clock::now() - start).count());

	CHECK(millis == 50);
	CHECK(num_lookups == 1);

	printf("[%4d] simulation::run() returned: %s\n"
		, millis, ec.message().c_str());
}

TEST_CASE("resolve non-existent hostname", "resolver") {
	sim_config cfg;
	simulation sim(cfg);

	chrono::high_resolution_clock::time_point start = chrono::high_resolution_clock::now();
	num_lookups = 0;

	asio::io_service ios(sim, address_v4::from_string("40.30.20.10"));

	asio::ip::tcp::resolver resolver(ios);
	asio::ip::tcp::resolver::query q("non-existent.com", "8080");
	resolver.async_resolve(q, std::bind(&on_failed_name_lookup, _1, _2));

	boost::system::error_code ec;
	sim.run(ec);

	int millis = int(duration_cast<milliseconds>(chrono::high_resolution_clock::now() - start).count());

	CHECK(millis == 100);
	CHECK(num_lookups == 1);

	printf("[%4d] simulation::run() returned: %s\n"
		, millis, ec.message().c_str());
}

TEST_CASE("lookups resolve serially, compounding the latency", "resolver") {
	sim_config cfg;
	simulation sim(cfg);

	chrono::high_resolution_clock::time_point start = chrono::high_resolution_clock::now();
	num_lookups = 0;

	asio::io_service ios(sim, address_v4::from_string("40.30.20.10"));

	asio::ip::tcp::resolver resolver(ios);
	asio::ip::tcp::resolver::query q1("non-existent.com", "8080");
	asio::ip::tcp::resolver::query q2("non-existent.com", "8080");
	resolver.async_resolve(q1, std::bind(&on_failed_name_lookup, _1, _2));
	resolver.async_resolve(q2, std::bind(&on_failed_name_lookup, _1, _2));

	boost::system::error_code ec;
	sim.run(ec);

	int millis = int(duration_cast<milliseconds>(chrono::high_resolution_clock::now() - start).count());

	CHECK(millis == 200);
	CHECK(num_lookups == 2);

	printf("[%4d] simulation::run() returned: %s\n"
		, millis, ec.message().c_str());
}

TEST_CASE("resolve an IP address", "resolver") {
	sim_config cfg;
	simulation sim(cfg);

	chrono::high_resolution_clock::time_point start = chrono::high_resolution_clock::now();
	num_lookups = 0;

	asio::io_service ios(sim, address_v4::from_string("40.30.20.10"));

	asio::ip::tcp::resolver resolver(ios);
	asio::ip::tcp::resolver::query q("10.10.10.10", "8080");
	resolver.async_resolve(q, [](boost::system::error_code const& ec, asio::ip::tcp::resolver::iterator iter)
	{
		++num_lookups;
		std::vector<address_v4> expect = { address_v4::from_string("10.10.10.10") };

		auto expect_it = expect.begin();
		while (iter != asio::ip::tcp::resolver::iterator())
		{
			assert(iter->endpoint().address() == *expect_it);
			assert(iter->endpoint().port() == 8080);

			++iter;
			++expect_it;
		}

		assert(expect_it == expect.end());
	});

	boost::system::error_code ec;
	sim.run(ec);

	int millis = int(duration_cast<milliseconds>(chrono::high_resolution_clock::now() - start).count());

	CHECK(millis == 0);
	CHECK(num_lookups == 1);

	printf("[%4d] simulation::run() returned: %s\n"
		, millis, ec.message().c_str());
}

