#include "game.hpp"

#include <cstdio>
#include <inttypes.h>
#include <unistd.h>

namespace genie {

void host_start(MultiplayerHost& host) {
	host.eventloop();
}

void client_start(MultiplayerClient& client) {
	puts("start client");
	client.eventloop();
}

MultiplayerHost::MultiplayerHost(uint16_t port) : Multiplayer(port), sock(port) {
	puts("start host");
	t_worker = std::thread(host_start, std::ref(*this));
}

MultiplayerHost::~MultiplayerHost() {
	puts("closing host");
	sock.close();
#if linux
	pthread_cancel(t_worker.native_handle());
#endif
	t_worker.join();
	puts("host stopped");
}

void MultiplayerHost::eventloop() {
	printf("host started on 127.0.0.1:%" PRIu16 "\n", port);
	sock.eventloop(*this);
}

void MultiplayerHost::event_process(sockfd fd, Command &cmd) {
	switch (cmd.type) {
	case CmdType::TEXT:
		printf("TEXT: \"%s\"\n", cmd.text().c_str());
		break;
	}
}

#if windows
void MultiplayerHost::incoming(WSAPOLLFD &ev) {
	printf("new slave: fd %" PRIu64 "\n", ev.fd);
}

void MultiplayerHost::removepeer(SOCKET fd) {
	printf("drop slave: fd %" PRIu64 "\n", fd);
}
#else
void MultiplayerHost::incoming(epoll_event &ev) {
	printf("new slave: fd %d\n", ev.data.fd);
}

void MultiplayerHost::removepeer(int fd) {
	printf("drop slave: fd %d\n", fd);
}
#endif

void MultiplayerHost::shutdown() {
	puts("host shutdown");
}

MultiplayerClient::MultiplayerClient(uint16_t port) : Multiplayer(port), sock(port), activated(false) {
	sock.reuse();

	t_worker = std::thread(client_start, std::ref(*this));
}

MultiplayerClient::~MultiplayerClient() {
	puts("closing client");
	// NOTE sock.close() has to be in both bodies, because its behavior will be slightly different!
	if (t_worker.joinable()) {
		sock.close();
		t_worker.join();
	}
	else {
		sock.close();
	}
	puts("client stopped");
}

void MultiplayerClient::eventloop() {
	bool connected = false;

	// FIXME improve connection establishment tolerance
	/**
	 * het idee is (de volgende punten gecombineerd):
	 * - probeer een X aantal keer te verbinden, maar altijd minstens 2 keer
	 * - als een connect poging mislukt, kijk hoeveel tijd er sinds connect verstreken is.
	 *   als het minder dan 500ms was, wacht totdat er ongeveer intotaal 500ms is verstreken
	 * - geef pas op als er minimaal Y ms zijn verstreken
	 */

	for (unsigned tries = 3; tries && !connected; --tries) {
		int err;

		if (!(err = sock.connect())) {
			connected = true;
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	if (!connected) {
		fputs("failed to connect\n", stderr);
		return;
	}

	puts("connected");

	// FIXME event loop
	//sock.send(Command::text("mah boi"), false);
	puts("send text1");
	Command cmd = Command::text("mah boi");
	sock.send(cmd, false);
	puts("send text2");
	cmd = Command::text("you saved me!");
	sock.send(cmd, false);

	puts("wait");

	sleep(2);

	cmd = Command::text("whoah ship ship ship");
	sock.send(cmd, false);
	puts("sent text3");
}

}