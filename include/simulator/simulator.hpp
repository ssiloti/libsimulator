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

#ifndef SIMULATOR_HPP_INCLUDED
#define SIMULATOR_HPP_INCLUDED

#include <boost/config.hpp>
#include <boost/asio/detail/config.hpp>
#include <boost/asio/basic_deadline_timer.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <deque>
#include <mutex>

#if defined _MSC_VER && _MSC_VER < 1900
#include <stdio.h>
#include <stdarg.h>

namespace sim { namespace aux {
inline int snprintf(char* buf, int len, char const* fmt, ...)
{
	va_list lp;
	int ret;
	va_start(lp, fmt);
	ret = _vsnprintf(buf, len, fmt, lp);
	va_end(lp);
	if (ret < 0) { buf[len-1] = 0; ret = len-1; }
	return ret;
}
}}
#endif

#if defined BOOST_ASIO_HAS_STD_CHRONO
#include <chrono>
#else
#include <boost/chrono/duration.hpp>
#include <boost/chrono/time_point.hpp>
#include <boost/ratio.hpp>
#endif

#include <boost/system/error_code.hpp>
#include <boost/function.hpp>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <vector>
#include <functional>

#ifdef SIMULATOR_BUILDING_SHARED
#define SIMULATOR_DECL BOOST_SYMBOL_EXPORT
#elif defined SIMULATOR_LINKING_SHARED
#define SIMULATOR_DECL BOOST_SYMBOL_IMPORT
#else
#define SIMULATOR_DECL
#endif

#if !defined _MSC_VER || _MSC_VER > 1900
#define LIBSIMULATOR_USE_MOVE 1
#else
#define LIBSIMULATOR_USE_MOVE 0
#endif

namespace sim
{
	namespace aux
	{
		struct channel;
		struct packet;
		struct sink_forwarder;
	}

	// this is an interface for somthing that can accept incoming packets,
	// such as queues, sockets, NATs and TCP congestion windows
	struct SIMULATOR_DECL sink
	{
		virtual void incoming_packet(aux::packet p) = 0;

		// used for visualization
		virtual std::string label() const = 0;

		virtual std::string attributes() const { return "shape=box"; }
	};

	// this represents a network route (a series of sinks to pass a packet
	// through)
	struct SIMULATOR_DECL route
	{
		friend route operator+(route lhs, route rhs)
		{ return lhs.append(std::move(rhs)); }

		std::shared_ptr<sink> next_hop() const { return hops.front(); }
		std::shared_ptr<sink> pop_front()
		{
			std::shared_ptr<sink> ret(std::move(hops.front()));
			hops.erase(hops.begin());
			return ret;
		}
		void replace_last(std::shared_ptr<sink> s) { hops.back() = std::move(s); }
		void prepend(route const& r)
		{ hops.insert(hops.begin(), r.hops.begin(), r.hops.end()); }
		void prepend(std::shared_ptr<sink> s) { hops.insert(hops.begin(), std::move(s)); }
		route& append(route const& r)
		{ hops.insert(hops.end(), r.hops.begin(), r.hops.end()); return *this; }
		route& append(std::shared_ptr<sink> s) { hops.push_back(std::move(s)); return *this; }
		bool empty() const { return hops.empty(); }
		std::shared_ptr<sink> last() const
		{ return hops.back(); }

	private:
		std::deque<std::shared_ptr<sink>> hops;
	};

	void forward_packet(aux::packet p);

	struct simulation;

	namespace chrono
	{
#if defined BOOST_ASIO_HAS_STD_CHRONO
	using std::chrono::seconds;
	using std::chrono::milliseconds;
	using std::chrono::microseconds;
	using std::chrono::nanoseconds;
	using std::chrono::minutes;
	using std::chrono::hours;
	using std::chrono::duration_cast;
	using std::chrono::time_point;
	using std::chrono::duration;
#else
	using boost::chrono::seconds;
	using boost::chrono::milliseconds;
	using boost::chrono::microseconds;
	using boost::chrono::nanoseconds;
	using boost::chrono::minutes;
	using boost::chrono::hours;
	using boost::chrono::duration_cast;
	using boost::chrono::time_point;
	using boost::chrono::duration;
#endif

	// std.chrono / boost.chrono compatible high_resolution_clock using a simulated time
	struct SIMULATOR_DECL high_resolution_clock
	{
		typedef boost::int64_t rep;
#if defined BOOST_ASIO_HAS_STD_CHRONO
		typedef std::nano period;
		typedef std::chrono::time_point<high_resolution_clock, nanoseconds> time_point;
		typedef std::chrono::duration<boost::int64_t, std::nano> duration;
#else
		typedef boost::nano period;
		typedef time_point<high_resolution_clock, nanoseconds> time_point;
		typedef duration<boost::int64_t, boost::nano> duration;
#endif
		static const bool is_steady = true;
		static time_point now();

		// private interface
		static void fast_forward(high_resolution_clock::duration d);
	};

	} // chrono

	namespace asio
	{

	using boost::asio::buffer_size;
	using boost::asio::buffer_cast;
	using boost::asio::const_buffer;
	using boost::asio::mutable_buffer;
	using boost::asio::const_buffers_1;
	using boost::asio::mutable_buffers_1;
	using boost::asio::buffer;

	struct io_service;

	struct SIMULATOR_DECL high_resolution_timer
	{
		friend struct sim::simulation;

		typedef chrono::high_resolution_clock::time_point time_type;
		typedef chrono::high_resolution_clock::duration duration_type;

		explicit high_resolution_timer(io_service& io_service);
		high_resolution_timer(io_service& io_service,
			const time_type& expiry_time);
		high_resolution_timer(io_service& io_service,
			const duration_type& expiry_time);

		std::size_t cancel(boost::system::error_code& ec);
		std::size_t cancel();
		std::size_t cancel_one();
		std::size_t cancel_one(boost::system::error_code& ec);

		time_type expires_at() const;
		std::size_t expires_at(const time_type& expiry_time);
		std::size_t expires_at(const time_type& expiry_time
			, boost::system::error_code& ec);

		duration_type expires_from_now() const;
		std::size_t expires_from_now(const duration_type& expiry_time);
		std::size_t expires_from_now(const duration_type& expiry_time
			, boost::system::error_code& ec);

		void wait();
		void wait(boost::system::error_code& ec);

		void async_wait(boost::function<void(boost::system::error_code)> handler);

		io_service& get_io_service() const { return m_io_service; }

	private:

		void fire(boost::system::error_code ec);

		time_type m_expiration_time;
		boost::function<void(boost::system::error_code const&)> m_handler;
		io_service& m_io_service;
		bool m_expired;
	};

	typedef high_resolution_timer waitable_timer;

	namespace error = boost::asio::error;
	typedef boost::asio::null_buffers null_buffers;

	template <typename Protocol>
	struct socket_base
	{
		socket_base(io_service& ios)
			: m_io_service(ios)
			, m_open(false)
			, m_non_blocking(false)
			, m_max_receive_queue_size(64 * 1024)
		{
		}

		// io_control
		using non_blocking_io = boost::asio::socket_base::non_blocking_io;
		using reuse_address = boost::asio::socket_base::reuse_address;

		// socket options
		using send_buffer_size = boost::asio::socket_base::send_buffer_size;
		using receive_buffer_size = boost::asio::socket_base::receive_buffer_size;

		template <class Option>
		boost::system::error_code set_option(Option const&
			, boost::system::error_code& ec) { return ec; }

		boost::system::error_code set_option(receive_buffer_size const& op
			, boost::system::error_code& ec)
		{
			m_max_receive_queue_size = op.value();
			return ec;
		}

		boost::system::error_code set_option(send_buffer_size const&
			, boost::system::error_code& ec)
		{
			// TODO: implement
			return ec;
		}

		boost::system::error_code set_option(reuse_address const&
			, boost::system::error_code& ec)
		{
			// TODO: implement
			return ec;
		}

		template <class Option>
		boost::system::error_code get_option(Option&
			, boost::system::error_code& ec) { return ec; }

		boost::system::error_code get_option(receive_buffer_size& op
			, boost::system::error_code& ec)
		{
			op = m_max_receive_queue_size;
			return ec;
		}

		template <class IoControl>
		boost::system::error_code io_control(IoControl const&
			, boost::system::error_code& ec) { return ec; }

		template <class IoControl>
		void io_control(IoControl const&) {}

		boost::system::error_code io_control(non_blocking_io const& ioc
			, boost::system::error_code& ec)
		{ m_non_blocking = ioc.get(); return ec; }

		void io_control(non_blocking_io const& ioc)
		{ m_non_blocking = ioc.get(); }

		bool is_open() const
		{
			return m_open;
		}

		io_service& get_io_service() const { return m_io_service; }

		typedef int message_flags;

		// internal interface

		route get_incoming_route();
		route get_outgoing_route();

	protected:

		io_service& m_io_service;

		typename Protocol::endpoint m_bound_to;

		// this is an object implementing the sink interface, forwarding
		// packets to this socket. If this socket is destructed, this forwarder
		// is redirected to just drop packets. This is necessary since sinks
		// must be held by shared_ptr, and socket objects aren't.
		std::shared_ptr<aux::sink_forwarder> m_forwarder;

		// whether the socket is open or not
		bool m_open;

		// true if the socket is set to non-blocking mode
		bool m_non_blocking;

		// the max size of the incoming queue. This is to emulate the send and
		// receive buffers. This should also depend on the bandwidth, to not
		// make the queue size not grow too long in time.
		int m_max_receive_queue_size;

	};

	namespace ip {

	using boost::asio::ip::address;
	using boost::asio::ip::address_v4;
	using boost::asio::ip::address_v6;

	template<typename Protocol>
	struct basic_endpoint : boost::asio::ip::basic_endpoint<Protocol>
	{
		basic_endpoint(ip::address const& addr, int port)
			: boost::asio::ip::basic_endpoint<Protocol>(addr, port) {}
		basic_endpoint() : boost::asio::ip::basic_endpoint<Protocol>() {}
	};

	template <typename Protocol>
	struct basic_resolver_entry
	{
		typedef typename Protocol::endpoint endpoint_type;
		typedef Protocol protocol_type;

		basic_resolver_entry() {}
		basic_resolver_entry(
			const endpoint_type& ep
			, std::string const& host
			, std::string const& service)
			: m_endpoint(ep)
			, m_host_name(host)
			, m_service(service)
		{}

		endpoint_type endpoint() const { return m_endpoint; }
		std::string host_name() const { return m_host_name; }
		operator endpoint_type() const { return m_endpoint; }
		std::string service_name() const { return m_service; }

	private:
		endpoint_type m_endpoint;
		std::string m_host_name;
		std::string m_service;
	};

	template<typename Protocol>
	struct basic_resolver;

	template<typename Protocol>
	struct basic_resolver_iterator
	{
		friend struct basic_resolver<Protocol>;

		basic_resolver_iterator(): m_idx(-1) {}

		typedef basic_resolver_entry<Protocol> value_type;
		typedef value_type const& reference;

		bool operator!=(basic_resolver_iterator const& rhs) const
		{
			return !this->operator==(rhs);
		}

		bool operator==(basic_resolver_iterator const& rhs) const
		{
			// if the indices are identical, the iterators are too.
			// if they are different though, the iterators may still be identical,
			// if it's the end iterator
			if (m_idx == rhs.m_idx) return true;

			const bool lhs_end = (m_idx == -1) || (m_idx == m_results.size());
			const bool rhs_end = (rhs.m_idx == -1) || (rhs.m_idx == rhs.m_results.size());

			return lhs_end == rhs_end;
		}

		value_type operator*() const { return m_results[m_idx]; }
		value_type const* operator->() const { return &m_results[m_idx]; }

		basic_resolver_iterator& operator++() { ++m_idx; return *this; }
		basic_resolver_iterator operator++(int)
		{
			basic_resolver_iterator tmp(*this);
			++m_idx;
			return tmp;
		}

	private:

		std::vector<value_type> m_results;
		int m_idx;
	};

	template <typename Protocol>
	struct basic_resolver_query
	{
		basic_resolver_query(std::string const& hostname, char const* service)
			: m_hostname(hostname)
			, m_service(service)
		{}

		std::string const& host_name() const { return m_hostname; }
		std::string const& service_name() const { return m_service; }

	private:
		std::string m_hostname;
		std::string m_service;
	};

	template<typename Protocol>
	struct basic_resolver
	{
		basic_resolver(io_service& ios);

		typedef Protocol protocol_type;
		typedef basic_resolver_iterator<Protocol> iterator;
		typedef basic_resolver_query<Protocol> query;

		void cancel();

		void async_resolve(basic_resolver_query<Protocol> q,
			boost::function<void(boost::system::error_code const&,
				basic_resolver_iterator<Protocol>)> handler);

		//TODO: add remaining members

	private:

		void on_lookup(boost::system::error_code const& ec);

		struct result_t
		{
			chrono::high_resolution_clock::time_point completion_time;
			boost::system::error_code err;
			basic_resolver_iterator<Protocol> iter;
			boost::function<void(boost::system::error_code const&,
				basic_resolver_iterator<Protocol>)> handler;
		};

		io_service& m_ios;
		asio::high_resolution_timer m_timer;
		using queue_t = std::vector<result_t>;

		queue_t m_queue;
	};

	struct SIMULATOR_DECL udp
	{
		static udp v4() { return udp(AF_INET); }
		static udp v6() { return udp(AF_INET6); }

		typedef basic_endpoint<udp> endpoint;

		struct SIMULATOR_DECL socket : socket_base<udp>, sink
		{
			typedef ip::udp::endpoint endpoint_type;
			typedef ip::udp protocol_type;
			typedef socket lowest_layer_type;

			socket(io_service& ios);
			~socket();

			socket(socket const&) = delete;
			socket& operator=(socket const&) = delete;
#if LIBSIMULATOR_USE_MOVE
			socket(socket&&) = default;
			socket& operator=(socket&&) = default;
#endif

			lowest_layer_type& lowest_layer() { return *this; }

			udp::endpoint local_endpoint(boost::system::error_code& ec) const;
			udp::endpoint local_endpoint() const;

			boost::system::error_code bind(ip::udp::endpoint const& ep
				, boost::system::error_code& ec);
			void bind(ip::udp::endpoint const& ep);

			boost::system::error_code close();
			boost::system::error_code close(boost::system::error_code& ec);

			boost::system::error_code cancel(boost::system::error_code& ec);
			void cancel();

			boost::system::error_code open(udp protocol, boost::system::error_code& ec);
			void open(udp protocol);

			template<typename ConstBufferSequence>
			std::size_t send_to(const ConstBufferSequence& bufs
				, const udp::endpoint& destination
				, socket_base::message_flags flags
				, boost::system::error_code& ec)
			{
				std::vector<asio::const_buffer> b(bufs.begin(), bufs.end());
				if (m_send_handler) abort_send_handler();
				return send_to_impl(b, destination, flags, ec);
			}

			template<typename ConstBufferSequence>
			std::size_t send_to(const ConstBufferSequence& bufs
				, const udp::endpoint& destination)
			{
				std::vector<asio::const_buffer> b(bufs.begin(), bufs.end());
				if (m_send_handler) abort_send_handler();
				boost::system::error_code ec;
				std::size_t ret = send_to_impl(b, destination, 0, ec);
				if (ec) throw boost::system::system_error(ec);
				return ret;
			}

			void async_send(const asio::null_buffers& bufs
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler);

			void async_receive(asio::null_buffers const&
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				if (m_recv_handler) abort_recv_handler();
				async_receive_null_buffers_impl(NULL, handler);
			}

			template <class BufferSequence>
			void async_receive(BufferSequence const& bufs
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				std::vector<asio::mutable_buffer> b(bufs.begin(), bufs.end());
				if (m_recv_handler) abort_recv_handler();

				async_receive_from_impl(b, nullptr, 0, handler);
			}


			void async_receive_from(asio::null_buffers const&
				, udp::endpoint& sender
				, socket_base::message_flags /* flags */
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				if (m_recv_handler) abort_recv_handler();
				async_receive_null_buffers_impl(&sender, handler);
			}

			void async_receive_from(asio::null_buffers const&
				, udp::endpoint& sender
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				// TODO: does it make sense to receive null_buffers and still have a
				// sender argument?
				if (m_recv_handler) abort_recv_handler();
				async_receive_null_buffers_impl(&sender, handler);
			}

			template <class BufferSequence>
			void async_receive_from(BufferSequence const& bufs
				, udp::endpoint& sender
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				std::vector<asio::mutable_buffer> b(bufs.begin(), bufs.end());
				if (m_recv_handler) abort_recv_handler();

				async_receive_from_impl(b, &sender, 0, handler);
			}

			template <class BufferSequence>
			void async_receive_from(BufferSequence const& bufs
				, udp::endpoint& sender
				, socket_base::message_flags flags
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				std::vector<asio::mutable_buffer> b(bufs.begin(), bufs.end());
				if (m_recv_handler) abort_recv_handler();

				async_receive_from_impl(b, &sender, flags, handler);
			}
/*
			void async_read_from(null_buffers const&
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				if (m_recv_handler) abort_recv_handler();
				async_read_some_null_buffers_impl(handler);
			}
*/

			template <class BufferSequence>
			std::size_t receive_from(BufferSequence const& bufs
				, udp::endpoint& sender)
			{
				std::vector<asio::mutable_buffer> b(bufs.begin(), bufs.end());
				assert(!b.empty());
				if (m_recv_handler) abort_recv_handler();
				boost::system::error_code ec;
				std::size_t ret = receive_from_impl(b, &sender, 0, ec);
				if (ec) throw boost::system::system_error(ec);
				return ret;
			}

			template <class BufferSequence>
			std::size_t receive_from(BufferSequence const& bufs
				, udp::endpoint& sender
				, socket_base::message_flags)
			{
				std::vector<asio::mutable_buffer> b(bufs.begin(), bufs.end());
				assert(!b.empty());
				if (m_recv_handler) abort_recv_handler();
				boost::system::error_code ec;
				std::size_t ret = receive_from_impl(b, &sender, 0, ec);
				if (ec) throw boost::system::system_error(ec);
				return ret;
			}

			template <class BufferSequence>
			std::size_t receive_from(BufferSequence const& bufs
				, udp::endpoint& sender
				, socket_base::message_flags
				, boost::system::error_code& ec)
			{
				std::vector<asio::mutable_buffer> b(bufs.begin(), bufs.end());
				assert(!b.empty());
				if (m_recv_handler) abort_recv_handler();
				return receive_from_impl(b, &sender, 0, ec);
			}

			// TODO: support connect and remote_endpoint

			// internal interface

			// implements sink
			virtual void incoming_packet(aux::packet p) override final;
			virtual std::string label() const override final
			{ return m_bound_to.address().to_string(); }

			void async_receive_from_impl(std::vector<asio::mutable_buffer> const& bufs
				, udp::endpoint* sender
				, socket_base::message_flags flags
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler);

			std::size_t receive_from_impl(
				std::vector<asio::mutable_buffer> const& bufs
				, udp::endpoint* sender
				, socket_base::message_flags flags
				, boost::system::error_code& ec);

			void async_receive_null_buffers_impl(
				udp::endpoint* sender
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler);

		private:
			void maybe_wakeup_reader();
			void abort_send_handler();
			void abort_recv_handler();

			std::size_t send_to_impl(std::vector<asio::const_buffer> const& b
				, udp::endpoint const& dst, message_flags flags
				, boost::system::error_code& ec);

			// this is the next time we'll have an opportunity to send another
			// outgoing packet. This is used to implement the bandwidth constraints
			// of channels. This may be in the past, in which case it's OK to send
			// a packet immediately.
			chrono::high_resolution_clock::time_point m_next_send;

			// while we're blocked in an async_write_some operation, this is the
			// handler that should be called once we're done sending
			boost::function<void(boost::system::error_code const&, std::size_t)>
				m_send_handler;

			// if we have an outstanding read on this socket, this is set to the
			// handler.
			boost::function<void(boost::system::error_code const&, std::size_t)>
				m_recv_handler;

			// if we have an outstanding read operation, this is the buffer to
			// receive into
			std::vector<asio::mutable_buffer> m_recv_buffer;

			// if we have an outstanding receive operation, this may point to an
			// endpoint to fill in the senders IP in
			udp::endpoint* m_recv_sender;

			asio::high_resolution_timer m_recv_timer;
			asio::high_resolution_timer m_send_timer;

			// this is the incoming queue of packets for each socket
			std::vector<aux::packet> m_incoming_queue;

			bool m_recv_null_buffers;

			// the number of bytes in the incoming packet queue
			int m_queue_size;

			// our address family
			bool m_is_v4;
		};

		struct SIMULATOR_DECL resolver : basic_resolver<udp>
		{
			resolver(io_service& ios) : basic_resolver(ios) {}
		};

		int family() const { return m_family; }

		friend bool operator==(udp const& lhs, udp const& rhs)
		{ return lhs.m_family == rhs.m_family; }

		friend bool operator!=(udp const& lhs, udp const& rhs)
		{ return lhs.m_family != rhs.m_family; }

	private:
		// Construct with a specific family.
		explicit udp(int protocol_family)
			: m_family(protocol_family)
		{}

		int m_family;

	}; // udp

	struct SIMULATOR_DECL tcp
	{
		// temporary fix until the resolvers are implemented using our endpoint
		tcp(boost::asio::ip::tcp p) : m_family(p.family()) {}

		static tcp v4() { return tcp(AF_INET); }
		static tcp v6() { return tcp(AF_INET6); }

		int family() const { return m_family; }

		typedef basic_endpoint<tcp> endpoint;

		struct SIMULATOR_DECL socket : socket_base<tcp>, sink
		{
			typedef ip::tcp::endpoint endpoint_type;
			typedef ip::tcp protocol_type;
			typedef socket lowest_layer_type;

			explicit socket(io_service& ios);
// TODO: sockets are not movable right now unfortunately, because channels keep
// pointers to the socke object to deliver new packets.
/*
#if LIBSIMULATOR_USE_MOVE
			socket(socket&&) = default;
			socket& operator=(socket&&) = default;
#endif
*/
			~socket();

			boost::system::error_code close();
			boost::system::error_code close(boost::system::error_code& ec);
			boost::system::error_code open(tcp protocol, boost::system::error_code& ec);
			void open(tcp protocol);
			boost::system::error_code bind(ip::tcp::endpoint const& ep
				, boost::system::error_code& ec);
			void bind(ip::tcp::endpoint const& ep);
			tcp::endpoint local_endpoint(boost::system::error_code& ec) const;
			tcp::endpoint local_endpoint() const;
			tcp::endpoint remote_endpoint(boost::system::error_code& ec) const;
			tcp::endpoint remote_endpoint() const;

			lowest_layer_type& lowest_layer() { return *this; }

			void async_connect(tcp::endpoint const& target
				, boost::function<void(boost::system::error_code const&)> h);

			template <class ConstBufferSequence>
			void async_write_some(ConstBufferSequence const& bufs
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				std::vector<asio::const_buffer> b(bufs.begin(), bufs.end());
				if (m_send_handler) abort_send_handler();
				async_write_some_impl(b, handler);
			}

			void async_write_some(null_buffers const&
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& /* handler */)
			{
				if (m_send_handler) abort_send_handler();
				assert(false && "not supported yet");
//				async_write_some_null_buffers_impl(b, handler);
			}

			void async_read_some(null_buffers const&
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				if (m_recv_handler) abort_recv_handler();
				async_read_some_null_buffers_impl(handler);
			}

			template <class BufferSequence>
			std::size_t read_some(BufferSequence const& bufs
				, boost::system::error_code& ec)
			{
				assert(m_non_blocking && "blocking operations not supported");
				std::vector<asio::mutable_buffer> b(bufs.begin(), bufs.end());
				return read_some_impl(b, ec);
			}

			template <class ConstBufferSequence>
			std::size_t write_some(ConstBufferSequence const& bufs
				, boost::system::error_code& ec)
			{
				assert(m_non_blocking && "blocking operations not supported");
				std::vector<asio::const_buffer> b(bufs.begin(), bufs.end());
				return write_some_impl(b, ec);
			}

			template <class BufferSequence>
			void async_read_some(BufferSequence const& bufs
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				std::vector<asio::mutable_buffer> b(bufs.begin(), bufs.end());
				if (m_recv_handler) abort_recv_handler();

				async_read_some_impl(b, handler);
			}

			std::size_t available(boost::system::error_code & ec) const;
			std::size_t available() const;

			boost::system::error_code cancel(boost::system::error_code& ec);
			void cancel();

			using socket_base::set_option;
			using socket_base::get_option;
			using socket_base::io_control;

			// private interface

			// implements sink
			virtual void incoming_packet(aux::packet p) override;
			virtual std::string label() const override final
			{ return m_bound_to.address().to_string(); }

			void internal_connect(tcp::endpoint const& bind_ip
				, std::shared_ptr<aux::channel> const& c
				, boost::system::error_code& ec);

			void abort_send_handler();
			void abort_recv_handler();

			virtual bool internal_is_listening();
		protected:

			void maybe_wakeup_reader();
			void maybe_wakeup_writer();

			void async_write_some_impl(std::vector<asio::const_buffer> const& bufs
				, boost::function<void(boost::system::error_code const&, std::size_t)> const& handler);
			void async_read_some_impl(std::vector<asio::mutable_buffer> const& bufs
				, boost::function<void(boost::system::error_code const&, std::size_t)> const& handler);
			void async_read_some_null_buffers_impl(
				boost::function<void(boost::system::error_code const&, std::size_t)> const& handler);
			std::size_t write_some_impl(std::vector<asio::const_buffer> const& bufs
				, boost::system::error_code& ec);
			std::size_t read_some_impl(std::vector<asio::mutable_buffer> const& bufs
				, boost::system::error_code& ec);

			void send_packet(aux::packet p);

			// called when a packet is dropped
			void packet_dropped(aux::packet p);

			boost::function<void(boost::system::error_code const&)> m_connect_handler;

			asio::high_resolution_timer m_connect_timer;

			// the tcp "packet size" (segment size)
			int m_mss;

			// while we're blocked in an async_write_some operation, this is the
			// handler that should be called once we're done sending
			boost::function<void(boost::system::error_code const&, std::size_t)>
				m_send_handler;

			std::vector<asio::const_buffer> m_send_buffer;

			// this is the incoming queue of packets for each socket
			std::vector<aux::packet> m_incoming_queue;

			// the number of bytes in the incoming packet queue
			int m_queue_size;

			// if we have an outstanding read on this socket, this is set to the
			// handler.
			boost::function<void(boost::system::error_code const&, std::size_t)>
				m_recv_handler;

			// if we have an outstanding buffers to receive into, these are them
			std::vector<asio::mutable_buffer> m_recv_buffer;

			asio::high_resolution_timer m_recv_timer;

			// our address family
			bool m_is_v4;

			// true if the currently outstanding read operation is for null_buffers
			bool m_recv_null_buffers;

			// true if the currenly outstanding write operation is for null_buffers
			bool m_send_null_buffers;

			// if this socket is connected to another endpoint, this object is
			// shared between both sockets and contain information and state about
			// the channel.
			std::shared_ptr<aux::channel> m_channel;

			std::uint64_t m_next_outgoing_seq;
			std::uint64_t m_next_incoming_seq;

			// the sequence number of the last dropped packet. We should only cut
			// the cwnd in half once per round-trip. If a whole window is lost, we
			// need to only halve it once
			std::uint64_t m_last_drop_seq;

			// the current congestion window size (in bytes)
			int m_cwnd;

			// the number of bytes that have been sent but not ACKed yet
			int m_bytes_in_flight;

			// reorder buffer for when packets are dropped
			std::map<std::uint64_t, aux::packet> m_reorder_buffer;

			// the sizes of packets given their sequence number
			std::unordered_map<std::uint64_t, int> m_outstanding_packet_sizes;

			// packets to re-send (because they were dropped)
			std::vector<aux::packet> m_outgoing_packets;
		};

		struct SIMULATOR_DECL acceptor : socket
		{
			acceptor(io_service& ios);
			~acceptor();

			boost::system::error_code cancel(boost::system::error_code& ec);
			void cancel();

			void listen(int qs = -1);
			void listen(int qs, boost::system::error_code& ec);

			void async_accept(ip::tcp::socket& peer
				, boost::function<void(boost::system::error_code const&)> h);
			void async_accept(ip::tcp::socket& peer
				, ip::tcp::endpoint& peer_endpoint
				, boost::function<void(boost::system::error_code const&)> h);

			boost::system::error_code close(boost::system::error_code& ec);
			void close();

			// private interface

			// implements sink
			virtual void incoming_packet(aux::packet p) override final;
			virtual bool internal_is_listening() override final;

		private:
			// check the incoming connection queue to see if any connection in
			// there is ready to be accepted and delivered to the user
			void check_accept_queue();
			void do_check_accept_queue(boost::system::error_code const& ec);

			boost::function<void(boost::system::error_code const&)> m_accept_handler;

			// the number of half-open incoming connections this listen socket can
			// hold. If this is -1, this socket is not yet listening and incoming
			// connection attempts should be rejected.
			int m_queue_size_limit;

			// these are incoming connection attempts. Both half-open and
			// completely connected. When accepting a connection, this queue is
			// checked first before waiting for a connection attempt.
			typedef std::vector<std::shared_ptr<aux::channel> > incoming_conns_t;
			incoming_conns_t m_incoming_queue;

			// the socket to accept a connection into
			tcp::socket* m_accept_into;

			// the endpoint to write the remote endpoint into when accepting
			tcp::endpoint* m_remote_endpoint;

			// non copyable
			acceptor(acceptor const&);
			acceptor& operator=(acceptor const&);
		};

		struct SIMULATOR_DECL resolver : basic_resolver<tcp>
		{
			resolver(io_service& ios) : basic_resolver(ios) {}
		};

		friend bool operator==(tcp const& lhs, tcp const& rhs)
		{ return lhs.m_family == rhs.m_family; }

		friend bool operator!=(tcp const& lhs, tcp const& rhs)
		{ return lhs.m_family != rhs.m_family; }

	private:
		// Construct with a specific family.
		explicit tcp(int protocol_family)
			: m_family(protocol_family)
		{}

		int m_family;
	};

	} // ip

	using boost::asio::async_write;
	using boost::asio::async_read;

	// boost.asio compatible io_service class that simulates the network
	// and time.
	struct SIMULATOR_DECL io_service
	{
		struct work : boost::asio::io_service::work
		{
			work(io_service& ios) :
				boost::asio::io_service::work(ios.get_internal_service()) {}
		};

		io_service(sim::simulation& sim);
		io_service(sim::simulation& sim, ip::address const& ip);
		io_service(sim::simulation& sim, std::vector<ip::address> const& ips);
		io_service();
		~io_service();

#if LIBSIMULATOR_USE_MOVE
		// not copyable and non movable (it's not movable because we currently
		// keep pointers to the io_service instances in the simulator object)
		io_service(io_service const&) = delete;
		io_service(io_service&&) = delete;
		io_service& operator=(io_service const&) = delete;
		io_service& operator=(io_service&&) = delete;
#endif

		std::size_t run(boost::system::error_code& ec);
		std::size_t run();

		std::size_t poll(boost::system::error_code& ec);
		std::size_t poll();

		std::size_t poll_one(boost::system::error_code& ec);
		std::size_t poll_one();

		void stop();
		bool stopped() const;
		void reset();

		void dispatch(boost::function<void()> handler);
		void post(boost::function<void()> handler);

		// internal interface
		boost::asio::io_service& get_internal_service();

		void add_timer(high_resolution_timer* t);
		void remove_timer(high_resolution_timer* t);

		ip::tcp::endpoint bind_socket(ip::tcp::socket* socket, ip::tcp::endpoint ep
			, boost::system::error_code& ec);
		void unbind_socket(ip::tcp::socket* socket
			, ip::tcp::endpoint ep);

		ip::udp::endpoint bind_udp_socket(ip::udp::socket* socket, ip::udp::endpoint ep
			, boost::system::error_code& ec);
		void unbind_udp_socket(ip::udp::socket* socket
			, ip::udp::endpoint ep);

		std::shared_ptr<aux::channel> internal_connect(ip::tcp::socket* s
			, ip::tcp::endpoint const& target, boost::system::error_code& ec);

		route find_udp_socket(asio::ip::udp::socket const& socket
			, ip::udp::endpoint const& ep);

		route const& get_outgoing_route(ip::address ip) const
		{ return m_outgoing_route.find(ip)->second; }

		route const& get_incoming_route(ip::address ip) const
		{ return m_incoming_route.find(ip)->second; }

		int get_path_mtu(asio::ip::address source, asio::ip::address dest) const;
		std::vector<ip::address> const& get_ips() const { return m_ips; }

		sim::simulation& sim() { return m_sim; }

	private:

		sim::simulation& m_sim;
		std::vector<ip::address> m_ips;

		// these are determined by the configuration. They may include NATs and
		// DSL modems (queues)
		std::map<ip::address, route> m_outgoing_route;
		std::map<ip::address, route> m_incoming_route;

		bool m_stopped;
	};

	template <typename Protocol>
	route socket_base<Protocol>::get_incoming_route()
	{
		route ret = m_io_service.get_incoming_route(m_bound_to.address());
		assert(m_forwarder);
		ret.append(std::static_pointer_cast<sim::sink>(m_forwarder));
		return ret;
	}

	template <typename Protocol>
	route socket_base<Protocol>::get_outgoing_route()
	{
		return route(m_io_service.get_outgoing_route(m_bound_to.address()));
	}

	} // asio

	struct configuration;
	struct queue;

	// user supplied configuration of the network to simulate
	struct SIMULATOR_DECL configuration
	{
		// build the network
		virtual void build(simulation& sim) = 0;

		// return the hops on the network packets from src to dst need to traverse
		virtual route channel_route(asio::ip::address src
			, asio::ip::address dst) = 0;

		// return the hops an incoming packet to ep need to traverse before
		// reaching the socket (for instance a NAT)
		virtual route incoming_route(asio::ip::address ip) = 0;

		// return the hops an outgoing packet from ep need to traverse before
		// reaching the network (for instance a DSL modem)
		virtual route outgoing_route(asio::ip::address ip) = 0;

		// return the path MTU between the two IP addresses
		// For TCP sockets, this will be called once when the connection is
		// established. For UDP sockets it's called for every burst of packets
		// that are sent
		virtual int path_mtu(asio::ip::address ip1, asio::ip::address ip2) = 0;

		// called for every hostname lookup made by the client. ``reqyestor`` is
		// the node performing the lookup, ``hostname`` is the name being looked
		// up. Resolve the name into addresses and fill in ``result`` or set
		// ``ec`` if the hostname is not found or some other error occurs. The
		// return value is the latency of the lookup. The client's callback won't
		// be called until after waiting this long.
		virtual chrono::high_resolution_clock::duration hostname_lookup(
			asio::ip::address const& requestor
			, std::string hostname
			, std::vector<asio::ip::address>& result
			, boost::system::error_code& ec) = 0;
	};

	struct SIMULATOR_DECL default_config : configuration
	{
		default_config() : m_sim(nullptr) {}

		virtual void build(simulation& sim) override;
		virtual route channel_route(asio::ip::address src
			, asio::ip::address dst) override;
		virtual route incoming_route(asio::ip::address ip) override;
		virtual route outgoing_route(asio::ip::address ip) override;
		virtual int path_mtu(asio::ip::address ip1, asio::ip::address ip2)
			override;
		virtual chrono::high_resolution_clock::duration hostname_lookup(
			asio::ip::address const& requestor
			, std::string hostname
			, std::vector<asio::ip::address>& result
			, boost::system::error_code& ec) override;

	protected:
		std::shared_ptr<queue> m_network;
		std::map<asio::ip::address, std::shared_ptr<queue>> m_incoming;
		std::map<asio::ip::address, std::shared_ptr<queue>> m_outgoing;
		simulation* m_sim;
	};

	struct SIMULATOR_DECL simulation
	{
		// it calls fire() when a timer fires
		friend struct high_resolution_timer;

		simulation(configuration& config);

		std::size_t run(boost::system::error_code& ec);
		std::size_t run();

		std::size_t poll(boost::system::error_code& ec);
		std::size_t poll();

		std::size_t poll_one(boost::system::error_code& ec);
		std::size_t poll_one();

		void stop();
		bool stopped() const;
		void reset();
		// private interface

		void add_timer(asio::high_resolution_timer* t);
		void remove_timer(asio::high_resolution_timer* t);

		boost::asio::io_service& get_internal_service()
		{ return m_service; }

		asio::io_service& get_io_service() { return m_internal_ios; }

		asio::ip::tcp::endpoint bind_socket(asio::ip::tcp::socket* socket
			, asio::ip::tcp::endpoint ep
			, boost::system::error_code& ec);
		void unbind_socket(asio::ip::tcp::socket* socket
			, asio::ip::tcp::endpoint ep);

		asio::ip::udp::endpoint bind_udp_socket(asio::ip::udp::socket* socket
			, asio::ip::udp::endpoint ep
			, boost::system::error_code& ec);
		void unbind_udp_socket(asio::ip::udp::socket* socket
			, asio::ip::udp::endpoint ep);

		std::shared_ptr<aux::channel> internal_connect(asio::ip::tcp::socket* s
			, asio::ip::tcp::endpoint const& target, boost::system::error_code& ec);

		route find_udp_socket(
			asio::ip::udp::socket const& socket
			, asio::ip::udp::endpoint const& ep);

		configuration& config() const { return m_config; }

		void add_io_service(asio::io_service* ios);
		void remove_io_service(asio::io_service* ios);
		std::vector<asio::io_service*> get_all_io_services() const;

	private:
		struct timer_compare
		{
			bool operator()(asio::high_resolution_timer const* lhs
				, asio::high_resolution_timer const* rhs)
			{ return lhs->expires_at() < rhs->expires_at(); }
		};

		configuration& m_config;

		// these are the io services that represent nodes on the network
		std::unordered_set<asio::io_service*> m_nodes;

		// all non-expired timers
		typedef std::multiset<asio::high_resolution_timer*, timer_compare> timer_queue_t;
		timer_queue_t m_timer_queue;
		std::mutex m_timer_queue_mutex;
		// underlying message queue
		boost::asio::io_service m_service;

		// used for internal timers
		asio::io_service m_internal_ios;

		typedef std::map<asio::ip::tcp::endpoint, asio::ip::tcp::socket*>
			listen_sockets_t;
		typedef listen_sockets_t::iterator listen_socket_iter_t;
		listen_sockets_t m_listen_sockets;

		typedef std::map<asio::ip::udp::endpoint, asio::ip::udp::socket*>
			udp_sockets_t;
		typedef udp_sockets_t::iterator udp_socket_iter_t;
		udp_sockets_t m_udp_sockets;

		bool m_stopped;
	};

	namespace aux
	{
		struct SIMULATOR_DECL packet
		{
			packet()
				: type(uninitialized)
				, from(new asio::ip::udp::endpoint)
				, overhead{20}
				, seq_nr{0}
			{}

			// this is move-only
#if LIBSIMULATOR_USE_MOVE
			packet(packet const&) = delete;
			packet& operator=(packet const&) = delete;
			packet(packet&&) = default;
			packet& operator=(packet&&) = default;
#endif

			// to keep things simple, don't drop ACKs or errors
			bool ok_to_drop() const
			{
				return type != syn_ack && type != ack && type != error;
			}

			enum type_t
			{
				uninitialized, // invalid type (used for debugging)
				syn, // TCP connect
				syn_ack, // TCP connection accepted
				ack, // the seq_nr is interpreted as "we received this"
				error, // the error_code (ec) is set
				payload // the buffer is filled
			} type;

			boost::system::error_code ec;

			// actual payload
			std::vector<boost::uint8_t> buffer;

			// used for UDP packets
			// this is a unique_ptr just to make this type movable. the endpoint
			// itself isn't
#if LIBSIMULATOR_USE_MOVE
			std::unique_ptr<asio::ip::udp::endpoint> from;
#else
			std::shared_ptr<asio::ip::udp::endpoint> from;
#endif

			// the number of bytes of overhead for this packet. The total packet
			// size is the number of bytes in the buffer + this number
			int overhead;

			// each hop in the route will pop itself off and forward the packet to
			// the next hop
			route hops;

			// for SYN packets, this is set to the channel we're trying to
			// establish
			std::shared_ptr<aux::channel> channel;

			// sequence number of this packet (used for debugging)
			std::uint64_t seq_nr;

			// this function must be called with this packet in case the packet is
			// dropped.
#if LIBSIMULATOR_USE_MOVE
			std::unique_ptr<std::function<void(aux::packet)>> drop_fun;
#else
			std::shared_ptr<std::function<void(aux::packet)>> drop_fun;
#endif
		};

		struct SIMULATOR_DECL sink_forwarder : sink
		{
			sink_forwarder(sink* dst) : m_dst(dst) {}

			virtual void incoming_packet(packet p) override final
			{
				if (m_dst == nullptr) return;
				m_dst->incoming_packet(std::move(p));
			}

			virtual std::string label() const override final
			{ return m_dst ? m_dst->label() : ""; }

			void clear() { m_dst = nullptr; }

		private:
			sink* m_dst;
		};

		/* the channel can be in the following states:
			1. handshake-1 - the initiating socket has sent SYN
			2. handshake-2 - the accepting connection has sent SYN+ACK
			3. handshake-3 - the initiating connection has received the SYN+ACK and
			                 considers the connection open, but the 3rd handshake
			                 message is still in flight.
			4. connected   - the accepting side has received the 3rd handshake
			                 packet and considers it open

			Whenever a connection attempt is made to a listening socket, as long as
			there is still space in the incoming socket queue, the accepting side
			will always respond immediately and complete the handshake, then wait
			until the user calls async_accept (which in this case would complete
			immediately).
		*/
		struct SIMULATOR_DECL channel
		{
			channel() {}
			// index 0 is the incoming route to the socket that initiated the connection.
			// index 1 may be empty while the connection is half-open
			route hops[2];

			// the endpoint of each end of the channel
			asio::ip::tcp::endpoint ep[2];

			int remote_idx(asio::ip::tcp::endpoint self) const;
			int self_idx(asio::ip::tcp::endpoint self) const;
		};

	} // aux

	void SIMULATOR_DECL dump_network_graph(simulation const& s, std::string filename);
}

#endif // SIMULATOR_HPP_INCLUDED

