#include "../src/engine.hpp"

#include <cstdio>

#include <stdexcept>
#include <thread>

#include <WinSock2.h>
#include <iphlpapi.h>
#pragma comment(lib, "IPHLPAPI.lib")

#include <memory>
#include <ctime>

namespace aoe {

static void net_start_stop() {
	Net net;
	(void)net;
}

static void net_start_twice() {
	Net net;
	try {
		// definitely not recommended to start the network subsystem twice, but it should work
		Net net2;
	} catch (std::exception &e) {
		fprintf(stderr, "%s: got %s\n", __func__, e.what());
	} catch (...) {
		fprintf(stderr, "%s: unknown error\n", __func__);
	}
}

static void adapters() {
	DWORD ret = 0;

	// NOTE ptr is an array, but only a[0] is valid! to get a[1] use a[0]->Next
	std::unique_ptr<IP_ADAPTER_INFO[]> a(new IP_ADAPTER_INFO[1]);
	ULONG sz = sizeof(a[0]), err;

	struct tm newtime;
	char buffer[32];
	errno_t error;

	// assume first attempt will fail as we have no idea what size to expect, but it might succeed
	if ((err = GetAdaptersInfo(&a[0], &sz)) != 0 && err != ERROR_BUFFER_OVERFLOW) {
		fprintf(stderr, "%s: GetAdaptersInfo: code %lu\n", __func__, err);
		return;
	}

	// resize and retry if failed
	if (err) {
		a.reset(new IP_ADAPTER_INFO[sz / sizeof(a[0]) + 1]);

		if ((err = GetAdaptersInfo(&a[0], &sz)) != 0) {
			fprintf(stderr, "%s: GetAdaptersInfo: code %lu\n", __func__, err);
			return;
		}
	}

	puts("Network adapters:");

	bool has_eth = false;

	for (PIP_ADAPTER_INFO ai = &a[0]; ai; ai = ai->Next) {
		printf("\tComboIndex: \t%d\n", ai->ComboIndex);
		printf("\tAdapter Name: \t%s\n", ai->AdapterName);
		printf("\tAdapter Desc: \t%s\n", ai->Description);
		printf("\tAdapter Addr: \t");
		for (UINT i = 0; i < ai->AddressLength; i++) {
			if (i == (ai->AddressLength - 1))
				printf("%.2X\n", (int)ai->Address[i]);
			else
				printf("%.2X-", (int)ai->Address[i]);
		}

		printf("\tIndex: \t%d\n", ai->Index);
		printf("\tType: \t");
		switch (ai->Type) {
			case MIB_IF_TYPE_OTHER:
				puts("Other");
				break;
			case MIB_IF_TYPE_ETHERNET:
				puts("Ethernet");
				has_eth = true;
				break;
			case MIB_IF_TYPE_TOKENRING:
				puts("Token Ring");
				break;
			case MIB_IF_TYPE_FDDI:
				puts("FDDI");
				break;
			case MIB_IF_TYPE_PPP:
				puts("PPP");
				break;
			case MIB_IF_TYPE_LOOPBACK:
				puts("Lookback");
				break;
			case MIB_IF_TYPE_SLIP:
				puts("Slip");
				break;
			default:
				printf("Unknown type %ld\n", ai->Type);
				break;
		}

		printf("\tIP Address: \t%s\n", ai->IpAddressList.IpAddress.String);
		printf("\tIP Mask: \t%s\n", ai->IpAddressList.IpMask.String);
		printf("\tGateway: \t%s\n\t***\n", ai->GatewayList.IpAddress.String);

		if (ai->DhcpEnabled) {
			puts("\tDHCP Enabled: Yes");
			printf("\t  DHCP Server: \t%s\n", ai->DhcpServer.IpAddress.String);

			printf("\t  Lease Obtained: ");
			/* Display local time */
			error = _localtime32_s(&newtime, (__time32_t *)&ai->LeaseObtained);
			if (error) {
				puts("Invalid Argument to _localtime32_s");
			} else {
				// Convert to an ASCII representation
				error = asctime_s(buffer, sizeof(buffer), &newtime);
				/* asctime_s returns the string terminated by \n\0 */
				printf("%s", error ? "Invalid Argument to asctime_s\n" : buffer);
			}

			printf("\t  Lease Expires:  ");
			error = _localtime32_s(&newtime, (__time32_t *)&ai->LeaseExpires);
			if (error) {
				puts("Invalid Argument to _localtime32_s");
			} else {
				// Convert to an ASCII representation
				error = asctime_s(buffer, sizeof(buffer), &newtime);
				/* asctime_s returns the string terminated by \n\0 */
				printf("%s", error ? "Invalid Argument to asctime_s\n" : buffer);
			}
		} else {
			puts("\tDHCP Enabled: No");
		}

		if (ai->HaveWins) {
			puts("\tHave Wins: Yes");
			printf("\t  Primary Wins Server:    %s\n", ai->PrimaryWinsServer.IpAddress.String);
			printf("\t  Secondary Wins Server:  %s\n", ai->SecondaryWinsServer.IpAddress.String);
		} else {
			puts("\tHave Wins: No");
		}
		putchar('\n');
	}

	putchar('\n');

	if (!has_eth)
		fprintf(stderr, "%s: no ethernet found\n", __func__);
}

static void tcp_init_too_early() {
	try {
		TcpSocket tcp;
		fprintf(stderr, "%s: net should not be initialised already\n", __func__);
	} catch (std::runtime_error&) {}
}

static void tcp_init_too_late() {
	try {
		{
			Net net;
		}
		TcpSocket tcp;
		fprintf(stderr, "%s: net should not be initialised anymore\n", __func__);
	} catch (std::runtime_error&) {}
}

static void tcp_init() {
	try {
		Net net;
		TcpSocket tcp;
	} catch (std::exception &e) {
		fprintf(stderr, "%s: got %s\n", __func__, e.what());
	}
}

static void tcp_init_all() {
	tcp_init_too_early();
	tcp_init_too_late();
	tcp_init();
}

static void tcp_bad_accept() {
	Net net;
	TcpSocket tcp;
	SOCKET s = tcp.accept();
	if (s != INVALID_SOCKET)
		fprintf(stderr, "%s: s is a socket: s=%p\n", __func__, (void*)s);
}

static void tcp_bind_dummy() {
	Net net;
	TcpSocket tcp(INVALID_SOCKET);
	try {
		tcp.bind("127.0.0.1", 80);
		fprintf(stderr, "%s: invalid socket has been bound successfully\n", __func__);
	} catch (std::runtime_error&) {}
}

static void tcp_bind_bad_address() {
	Net net;
	TcpSocket tcp;
	try {
		tcp.bind("a.b.c.d", 1234);
		fprintf(stderr, "%s: should not recognize a.b.c.d\n", __func__);
	} catch (std::runtime_error&) {}
}

static void tcp_bind() {
	Net net;
	TcpSocket tcp;
	tcp.bind("127.0.0.1", 80);
}

static void tcp_bind_twice() {
	Net net;
	TcpSocket tcp, tcp2;
	try {
		tcp.bind("127.0.0.1", 80);
		tcp2.bind("127.0.0.1", 80);
		fprintf(stderr, "%s: should not bind to 80\n", __func__);
	} catch (std::runtime_error&) {}
}

static void tcp_bind_all() {
	tcp_bind_bad_address();
	tcp_bind_twice();
	tcp_bind();
}

static void tcp_listen_too_early() {
	Net net;
	TcpSocket tcp;
	try {
		tcp.listen(50);
		fprintf(stderr, "%s: should have called bind(address, port) first\n", __func__);
	} catch (std::runtime_error&) {}
}

static void tcp_listen_bad_backlog() {
	Net net;
	TcpSocket tcp;
	try {
		tcp.bind("127.0.0.1", 80);
		tcp.listen(-1);
		fprintf(stderr, "%s: backlog cannot be negative\n", __func__);
		tcp.listen(0);
		fprintf(stderr, "%s: backlog must be positive\n", __func__);
	} catch (std::runtime_error&) {}
}

static void tcp_listen_twice() {
	Net net;
	TcpSocket tcp;
	try {
		tcp.bind("127.0.0.1", 80);
		tcp.listen(50);
		tcp.listen(50);
	} catch (std::runtime_error &e) {
		fprintf(stderr, "%s: got %s\n", __func__, e.what());
	}
}

static void tcp_listen_all() {
	tcp_listen_too_early();
	tcp_listen_bad_backlog();
	tcp_listen_twice();
}

static void tcp_connect_bad_address() {
	Net net;
	TcpSocket tcp;
	try {
		tcp.connect("a.b.c.d", 80);
		fprintf(stderr, "%s: should not recognize a.b.c.d\n", __func__);
	} catch (std::runtime_error&) {}
}

static void tcp_connect_timeout() {
	Net net;
	TcpSocket tcp;
	try {
		tcp.connect("127.0.0.1", 1234);
		fprintf(stderr, "%s: should timeout\n", __func__);
	} catch (std::runtime_error&) {}
}

static void main_accept() {
	try {
		TcpSocket server;
		server.bind("127.0.0.1", 80);
		server.listen(1);
		SOCKET s = server.accept();
		//printf("%s: s=%p\n", __func__, (void*)s);
	} catch (std::runtime_error &e) {
		fprintf(stderr, "%s: got %s\n", __func__, e.what());
	}
}

static void main_connect() {
	try {
		TcpSocket client;
		client.connect("127.0.0.1", 80);
	} catch (std::runtime_error &e) {
		fprintf(stderr, "%s: got %s\n", __func__, e.what());
	}
}

static void main_receive_int(int chk, bool equal) {
	try {
		TcpSocket server;

		server.bind("127.0.0.1", 80);
		server.listen(1);

		SOCKET s = server.accept();
		TcpSocket peer(s);

		int v = chk - 1, in;
		in = peer.recv(&v, 1);

		if (equal) {
			if (in != 1)
				fprintf(stderr, "%s: requested %u bytes, but %d %s received\n", __func__, (unsigned)(sizeof v), in, in == 1 ? " byte" : "bytes");
			else if (v != chk)
				fprintf(stderr, "%s: expected 0x%X, got 0x%X (xor: 0x%X)\n", __func__, chk, v, chk ^ v);
		} else {
			if (in == 1)
				fprintf(stderr, "%s: requested %u bytes and %d %s received\n", __func__, (unsigned)(sizeof v), in, in == 1 ? " byte" : "bytes");
			else if (v == chk)
				fprintf(stderr, "%s: did not expect %d\n", __func__, chk);
		}
	} catch (std::runtime_error &e) {
		fprintf(stderr, "%s: got %s\n", __func__, e.what());
	}
}

static void main_send_int(int v) {
	try {
		TcpSocket client;

		client.connect("127.0.0.1", 80);
		int out = client.send(&v, 1);

		if (out != 1)
			fprintf(stderr, "%s: written %u bytes, but %d %s sent\n", __func__, (unsigned)v, out, out == 1 ? "byte" : "bytes");
	} catch (std::runtime_error &e) {
		fprintf(stderr, "%s: got %s\n", __func__, e.what());
	}
}

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

static void main_exchange_receive(unsigned step)
{
	try {
		TcpSocket server;

		server.bind("127.0.0.1", 1234);
		server.listen(1);

		SOCKET s = server.accept();
		TcpSocket peer(s);

		unsigned tbl[256]{ 0 };

		peer.recv_fully(tbl, ARRAY_SIZE(tbl));

		for (unsigned i = 0; i < ARRAY_SIZE(tbl); ++i) {
			if (tbl[i] != i * step) {
				fprintf(stderr, "%s: expected %u, got %u\n", __func__, i * step, tbl[i]);
				break;
			}
		}

		int dummy = 0;
		peer.send_fully(&dummy, 1);
	} catch (std::runtime_error &e) {
		fprintf(stderr, "%s: got %s\n", __func__, e.what());
	}
}

static void main_exchange_send(unsigned step)
{
	try {
		TcpSocket client;
		client.connect("127.0.0.1", 1234);

		unsigned tbl[256]{ 0 };

		for (unsigned i = 0; i < ARRAY_SIZE(tbl); ++i) {
			tbl[i] = i * step;
		}

		client.send_fully(&tbl[0], ARRAY_SIZE(tbl) / 2);
		client.send_fully(&tbl[ARRAY_SIZE(tbl) - ARRAY_SIZE(tbl) / 2], ARRAY_SIZE(tbl) / 2);

		int dummy;

		client.recv_fully(&dummy, 1);
	} catch (std::runtime_error &e) {
		fprintf(stderr, "%s: got %s\n", __func__, e.what());
	}
}

static void tcp_connect() {
	Net net;
	std::thread t1(main_accept), t2(main_connect);
	t1.join();
	t2.join();
}

static void tcp_exchange() {
	Net net;
	std::thread t1(main_exchange_receive, 5), t2(main_exchange_send, 5);
	t1.join();
	t2.join();
}

static void tcp_exchange_int() {
	Net net;
	int chk = 0xcafebabe;
	// assumes sockets on localhost always send and receive all data in a single call
	std::thread t1(main_receive_int, chk, true), t2(main_send_int, chk);
	t1.join();
	t2.join();
}

static void tcp_send_fail() {
	Net net;
	int chk = 0xcafebabe;
	std::thread t1(main_receive_int, chk, false), t2(main_connect);
	t1.join();
	t2.join();
}

static void tcp_connect_all() {
	tcp_connect_bad_address();
	tcp_connect_timeout();
	tcp_connect();
	tcp_exchange();
}

static void tcp_runall() {
	puts("tcp");
	tcp_init_all();
	tcp_bad_accept();
	tcp_bind_all();
	tcp_listen_all();
	tcp_connect_all();
	tcp_exchange_int();
	tcp_send_fail();
}

void net_runall() {
	puts("net");
	net_start_stop();
	net_start_twice();
	adapters();
	tcp_runall();
}

}