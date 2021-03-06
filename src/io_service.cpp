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

#include <boost/make_shared.hpp>
#include <boost/system/error_code.hpp>

namespace sim { namespace asio {

	io_service::io_service(sim::simulation& sim)
		: io_service(sim, std::vector<asio::ip::address>())
	{}

	io_service::io_service(sim::simulation& sim, asio::ip::address const& ip)
		: io_service(sim, std::vector<asio::ip::address>{ip})
	{}

	io_service::io_service(sim::simulation& sim, std::vector<asio::ip::address> const& ips)
		: m_sim(sim)
		, m_ips(ips)
		, m_stopped(false)
	{
		for (auto const& ip : m_ips)
		{
			m_outgoing_route[ip] = m_sim.config().outgoing_route(ip);
			m_incoming_route[ip] = m_sim.config().incoming_route(ip);
		}
		m_sim.add_io_service(this);
	}

	io_service::~io_service()
	{
		m_sim.remove_io_service(this);
	}

	io_service::io_service()
		: m_sim(*reinterpret_cast<sim::simulation*>(NULL))
	{
		assert(false);
	}

	int io_service::get_path_mtu(asio::ip::address source, asio::ip::address dest) const
	{
		// TODO: it would be nice to actually traverse the virtual network nodes
		// and ask for their MTU instead
		assert(std::count(m_ips.begin(), m_ips.end(), source) > 0 && "source address must be a local address to this node/io_service");
		return m_sim.config().path_mtu(source, dest);
	}

	void io_service::stop()
	{
		// TODO: cancel all outstanding handler associated with this io_service
		m_stopped = true;
	}

	bool io_service::stopped() const
	{
		return m_stopped;
	}

	void io_service::reset()
	{
		m_stopped = false;
	}

	std::size_t io_service::run()
	{
		assert(false);
		return 0;
	}

	std::size_t io_service::run(boost::system::error_code& ec)
	{
		assert(false);
		return 0;
	}

	std::size_t io_service::poll()
	{
		assert(false);
		return 0;
	}

	std::size_t io_service::poll(boost::system::error_code& ec)
	{
		assert(false);
		return 0;
	}

	std::size_t io_service::poll_one()
	{
		assert(false);
		return 0;
	}

	std::size_t io_service::poll_one(boost::system::error_code& ec)
	{
		assert(false);
		return 0;
	}

	void io_service::dispatch(boost::function<void()> handler)
	{ m_sim.get_internal_service().dispatch(handler); }

	void io_service::post(boost::function<void()> handler)
	{ m_sim.get_internal_service().post(handler); }

	// private interface

	void io_service::add_timer(high_resolution_timer* t)
	{
		m_sim.add_timer(t);
	}

	void io_service::remove_timer(high_resolution_timer* t)
	{
		m_sim.remove_timer(t);
	}

	boost::asio::io_service& io_service::get_internal_service()
	{ return m_sim.get_internal_service(); }

	ip::tcp::endpoint io_service::bind_socket(ip::tcp::socket* socket
		, ip::tcp::endpoint ep, boost::system::error_code& ec)
	{
		assert(!m_ips.empty() && "you cannot use an internal io_service (one without an IP address) for creating and binding sockets");
		if (ep.address() == ip::address_v4::any())
		{
			auto it = std::find_if(m_ips.begin(), m_ips.end()
				, [](ip::address const& ip) { return ip.is_v4(); } );
			if (it == m_ips.end())
			{
				ec.assign(boost::system::errc::address_not_available
					, boost::system::generic_category());
				return ip::tcp::endpoint();
			}
			// TODO: pick the first local endpoint for now. In the future we may
			// want have a bias toward
			ep.address(*it);
		}
		else if (ep.address() == ip::address_v6::any())
		{
			auto it = std::find_if(m_ips.begin(), m_ips.end()
				, [](ip::address const& ip) { return ip.is_v6(); } );
			if (it == m_ips.end())
			{
				ec.assign(boost::system::errc::address_not_available
					, boost::system::generic_category());
				return ip::tcp::endpoint();
			}
			// TODO: pick the first local endpoint for now. In the future we may
			// want have a bias toward
			ep.address(*it);
		}
		else if (std::count(m_ips.begin(), m_ips.end(), ep.address()) == 0)
		{
			// you can only bind to the IP assigned to this node.
			// TODO: support loopback
			ec.assign(boost::system::errc::address_not_available
				, boost::system::generic_category());
			return ip::tcp::endpoint();
		}

		return m_sim.bind_socket(socket, ep, ec);
	}

	void io_service::unbind_socket(ip::tcp::socket* socket
		, ip::tcp::endpoint ep)
	{
		m_sim.unbind_socket(socket, ep);
	}

	ip::udp::endpoint io_service::bind_udp_socket(ip::udp::socket* socket
		, ip::udp::endpoint ep, boost::system::error_code& ec)
	{
		assert(!m_ips.empty() && "you cannot use an internal io_service (one without an IP address) for creating and binding sockets");
		if (ep.address() == ip::address_v4::any())
		{
			auto it = std::find_if(m_ips.begin(), m_ips.end()
				, [](ip::address const& ip) { return ip.is_v4(); });
			if (it == m_ips.end())
			{
				ec.assign(boost::system::errc::address_not_available
						  , boost::system::generic_category());
				return ip::udp::endpoint();
			}
			// TODO: pick the first local endpoint for now. In the future we may
			// want have a bias toward
			ep.address(*it);
		}
		else if (ep.address() == ip::address_v6::any())
		{
			auto it = std::find_if(m_ips.begin(), m_ips.end()
				, [](ip::address const& ip) { return ip.is_v6(); });
			if (it == m_ips.end())
			{
				ec.assign(boost::system::errc::address_not_available
						  , boost::system::generic_category());
				return ip::udp::endpoint();
			}
			// TODO: pick the first local endpoint for now. In the future we may
			// want have a bias toward
			ep.address(*it);
		}
		else if (std::count(m_ips.begin(), m_ips.end(), ep.address()) == 0)
		{
			// you can only bind to the IP assigned to this node.
			// TODO: support loopback
			ec.assign(boost::system::errc::address_not_available
				, boost::system::generic_category());
			return ip::udp::endpoint();
		}

		return m_sim.bind_udp_socket(socket, ep, ec);
	}

	void io_service::unbind_udp_socket(ip::udp::socket* socket
		, ip::udp::endpoint ep)
	{
		m_sim.unbind_udp_socket(socket, ep);
	}

	std::shared_ptr<aux::channel> io_service::internal_connect(ip::tcp::socket* s
		, ip::tcp::endpoint const& target, boost::system::error_code& ec)
	{
		return m_sim.internal_connect(s, target, ec);
	}

	route io_service::find_udp_socket(asio::ip::udp::socket const& socket
		, ip::udp::endpoint const& ep)
	{
		return m_sim.find_udp_socket(socket, ep);
	}

} // asio
} // sim

