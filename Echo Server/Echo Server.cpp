#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "Common.h" // err_quit, err_display
#include "NetCommon.h" // 기존의 에코 서버의 상태 관리 구조체, 헤더 구조체, 송 / 수신 함수
#include <thread>
#include <memory>
#include "socketRAII.h" // 기존의 에코 서버의 RAII 객체들
#include "LineLogger.h"
#include "Types.h"
#include "Manager.h"
#include "Room.h"
#include "ClientSession.h"

#pragma comment(lib, "Ws2_32.lib")

// OOP 싫어... RAII 싫어.. 근데 왜 재밌냐 시발

void client_thread(std::shared_ptr<ClientSession> session) { // detach()로 분리한 스레드
	session->Run();

	return;
}

void client_recv_thread(std::shared_ptr<ClientSession> session) {
	session->RecvRun();

	LineLogger::GetInstance().WriteLog("[Thread Exit] recv thread finished. SessionID = ", session->GetSessionID());

	return;
}

void client_send_thread(std::shared_ptr<ClientSession> session) {
	session->SendRun();

	LineLogger::GetInstance().WriteLog("[Thread Exit] send thread finished. SessionID = ", session->GetSessionID());

	return;
}

int main() {
	try {
		WinsockGuard winsock;

		auto manager = std::make_shared<Manager>();
		SessionID next_session_id = INITIAL_SESSION_ID;

		ListenSocket server_sock;
		sockaddr_in server_addr{};
		server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		server_addr.sin_family = AF_INET;
		server_addr.sin_port = htons(SERVER_PORT);

		server_sock.ListenSockBind(&server_addr);

		server_sock.ListenSockListen();

		while (true) {
			sockaddr_in client_addr{};
			try {

				auto client_socket = std::make_unique<ClientSocket>(server_sock.ListenSockAccept(&client_addr));
				auto client_session = std::make_shared<ClientSession>(std::move(client_socket), client_addr, next_session_id);

				manager->AddClient(client_session, next_session_id);
				next_session_id += 1;

				std::thread(client_recv_thread, client_session).detach();
				std::thread(client_send_thread, client_session).detach();
		    }
			catch (const std::exception& e) {
				std::cerr << e.what() << '\n';
				continue;
			}


		}


	}
	catch(const std::exception& e) {
		std::cerr << e.what() << '\n';
	}

	return 0;
}
