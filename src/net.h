#ifndef NET_H
#define NET_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <cinttypes>

namespace net
{
	using u8 = std::uint8_t;
	using u16 = std::uint16_t;
	using u32 = std::uint32_t;

	class Address
	{
	public:

		Address() : _addr(0), _port(0) {}

		Address(u8 a, u8 b, u8 c, u8 d, u16 port)
			: _addr((a << 24) | (b << 16) | (c << 8) | d)
			, _port(port)
		{}

		Address( u32 address, u16 port )
			: _addr(address)
			, _port(port)
		{}

		u32 addr() const { return _addr; }
		u16 port() const { return _port; }

		u8 a() const { return static_cast<u8>(_addr >> 24); }
		u8 b() const { return static_cast<u8>(_addr >> 16); }
		u8 c() const { return static_cast<u8>(_addr >>  8); }
		u8 d() const { return static_cast<u8>(_addr      ); }


		bool operator == ( const Address & other ) const
		{
			return _addr == other._addr && _port == other._port;
		}

		bool operator != ( const Address & other ) const
		{
			return ! ( *this == other );
		}

		bool operator < ( const Address & other ) const
		{
			// note: this is so we can use _addr as a key in std::map
			if ( _addr < other._addr )
				return true;
			if ( _addr > other._addr )
				return false;
			else
				return _port < other._port;
		}

	private:

		u32 _addr;
		u16 _port;
	};

	class Socket
	{
	public:

		Socket() { soc = 0; }

		~Socket() { close(); }

		bool open( u16 port, int flags )
		{
			assert( !is_open() );
			#if defined(__APPLE__)
			bool want_nonblock = flags == 1;
			flags = 0;
			#endif

			soc = ::socket( AF_INET, SOCK_DGRAM | flags, IPPROTO_UDP );

			if (soc <= 0) {
				soc = 0;
				return false;
			}

			#if defined(__APPLE__)
			if (want_nonblock)
				fcntl(soc, F_SETFL, O_NONBLOCK);
			#endif

			sockaddr_in address;
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = INADDR_ANY;
			address.sin_port = htons(port);

			if (bind(soc, (const sockaddr*) &address, sizeof(sockaddr_in)) < 0) {
				close();
				return false;
			}

			return true;
		}

		bool open(u16 port)
		{
			return open(port, 0);
		}

		void close()
		{
			if (soc != 0) {
				::close(soc);
				soc = 0;
			}
		}

		bool is_open() const { return soc != 0; }

		bool send(const Address & destination, const void * data, int size)
		{
			assert(data    );
			assert(size > 0);

			if (soc == 0)
				return false;

			assert(destination.addr() != 0);
			assert(destination.port() != 0);

			sockaddr_in address;
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(destination.addr());
			address.sin_port = htons(destination.port());

			int sent_bytes = sendto(soc, (const char*)data, size, 0, (sockaddr*)&address, sizeof(sockaddr_in));

			return sent_bytes == size;
		}

		int receive(Address & sender, void * data, int size)
		{
			assert(data);
			assert(size > 0);

			if (soc == 0)
				return 0;

			sockaddr_in from;
			socklen_t fromLength = sizeof( from );

			int received_bytes = recvfrom( soc, (char*)data, size, 0, (sockaddr*)&from, &fromLength );

			if (received_bytes <= 0)
				return 0;

			u32 address = ntohl(from.sin_addr.s_addr);
			u16 port = ntohs(from.sin_port);

			sender = Address(address, port);

			return received_bytes;
		}

	private:

		int soc;
	};
}

#endif