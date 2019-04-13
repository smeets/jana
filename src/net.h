#ifndef NET_H
#define NET_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>

namespace net
{
	class Address
	{
	public:

		Address()
		{
			address = 0;
			port = 0;
		}

		Address( unsigned char a, unsigned char b, unsigned char c, unsigned char d, unsigned short port )
		{
			this->address = ( a << 24 ) | ( b << 16 ) | ( c << 8 ) | d;
			this->port = port;
		}

		Address( unsigned int address, unsigned short port )
		{
			this->address = address;
			this->port = port;
		}

		unsigned int GetAddress() const
		{
			return address;
		}

		unsigned char GetA() const
		{
			return ( unsigned char ) ( address >> 24 );
		}

		unsigned char GetB() const
		{
			return ( unsigned char ) ( address >> 16 );
		}

		unsigned char GetC() const
		{
			return ( unsigned char ) ( address >> 8 );
		}

		unsigned char GetD() const
		{
			return ( unsigned char ) ( address );
		}

		unsigned short GetPort() const
		{
			return port;
		}

		bool operator == ( const Address & other ) const
		{
			return address == other.address && port == other.port;
		}

		bool operator != ( const Address & other ) const
		{
			return ! ( *this == other );
		}

		bool operator < ( const Address & other ) const
		{
			// note: this is so we can use address as a key in std::map
			if ( address < other.address )
				return true;
			if ( address > other.address )
				return false;
			else
				return port < other.port;
		}

	private:

		unsigned int address;
		unsigned short port;
	};

	class Socket
	{
	public:

		Socket()
		{
			soc = 0;
		}

		~Socket()
		{
			Close();
		}

		bool Open( unsigned short port, int flags )
		{
			assert( !IsOpen() );

			soc = ::socket( AF_INET, SOCK_DGRAM | flags, IPPROTO_UDP );

			if (soc <= 0) {
				soc = 0;
				return false;
			}

			sockaddr_in address;
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = INADDR_ANY;
			address.sin_port = htons( (unsigned short) port );

			if (bind(soc, (const sockaddr*) &address, sizeof(sockaddr_in)) < 0) {
				Close();
				return false;
			}

			return true;
		}

		bool Open(unsigned short port) {
			return Open(port, 0);
		}

		// bool SetBlocking()
		// {
		// 	int flags = fcntl(socket, F_GETFL);
		// 	if (fcntl(socket, F_SETFL, flags & (~O_NONBLOCK)) == -1)
		// 	{
		// 		printf( "failed to set blocking socket\n" );
		// 		Close();
		// 		return false;
		// 	}
		// 	return true;
		// }

		// bool SetNonBlocking()
		// {
		// 	int flags = fcntl(socket, F_GETFL);
		// 	flags = flags | O_NONBLOCK;
		// 	if (fcntl(socket, F_SETFL, &flags) == -1)
		// 	{
		// 		printf( "failed to set non-blocking socket\n" );
		// 		Close();
		// 		return false;
		// 	}
		// 	return true;
		// }

		void Close()
		{
			if ( soc != 0 )
			{
				close( soc );
				soc = 0;
			}
		}

		bool IsOpen() const
		{
			return soc != 0;
		}

		bool Send( const Address & destination, const void * data, int size )
		{
			assert( data );
			assert( size > 0 );

			if ( soc == 0 )
				return false;

			assert( destination.GetAddress() != 0 );
			assert( destination.GetPort() != 0 );

			sockaddr_in address;
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl( destination.GetAddress() );
			address.sin_port = htons( (unsigned short) destination.GetPort() );

			int sent_bytes = sendto( soc, (const char*)data, size, 0, (sockaddr*)&address, sizeof(sockaddr_in) );

			return sent_bytes == size;
		}

		int Receive( Address & sender, void * data, int size )
		{
			assert( data );
			assert( size > 0 );

			if ( soc == 0 )
				return false;

			sockaddr_in from;
			socklen_t fromLength = sizeof( from );

			int received_bytes = recvfrom( soc, (char*)data, size, 0, (sockaddr*)&from, &fromLength );

			if ( received_bytes <= 0 )
				return 0;

			unsigned int address = ntohl( from.sin_addr.s_addr );
			unsigned short port = ntohs( from.sin_port );

			sender = Address( address, port );

			return received_bytes;
		}

	private:

		int soc;
	};
}

#endif