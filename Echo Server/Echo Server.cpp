#include <iostream>
#include <ws2tcpip.h>
#include <winsock2.h>
#include "Common.h" // err_quit, err_display
#include "NetCommon.h" // 기존의 에코 서버의 상태 관리 구조체, 헤더 구조체, 송 / 수신 함수
#include <thread>
#include <map>
#include <mutex>

#pragma comment(lib, "Ws2_32.lib")

// OOP 싫어... RAII 싫어.. 근데 왜 재밌냐 시발

class ClientSocket { // accept() 직후에 만들어지는 객체
private:
	SOCKET client_sock;
public:
	ClientSocket(SOCKET s) : client_sock(s) { }

	ClientSocket(const ClientSocket&) = delete;
	ClientSocket& operator=(const ClientSocket&) = delete;

	int ClientRecvAll(char* buf, int len, NetState& state) {

		int recv_all_res = recv_all(client_sock, state, buf, len);

		if (recv_all_res == SOCKET_ERROR) {
			return SOCKET_ERROR;
		}

		return recv_all_res;
	}

	int ClientSendAll(const char* msg, int len, NetState& state) {

		int send_all_res = send_all(client_sock, state, msg, len);

		if (send_all_res == SOCKET_ERROR) {
			return SOCKET_ERROR;
		}

		return send_all_res;
	}

	~ClientSocket() {
		if (client_sock != INVALID_SOCKET) {
			closesocket(client_sock);
		}
	}

	SOCKET get() const { return client_sock; }
};

class ListenSocket {
private:
	SOCKET sock;
public:
	ListenSocket() {
		sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCKET) {
			throw std::runtime_error("listen용 소켓 생성 실패");
		}
	}
	void ListenSockBind(const sockaddr_in* addr) {

		if (bind(sock, (sockaddr*) addr, sizeof(*addr)) == SOCKET_ERROR) {
			throw std::runtime_error("listen용 소켓 바인딩 실패");
		}
		return;
	}
	void ListenSockListen() {

		if (listen(sock, SOMAXCONN) == SOCKET_ERROR) {
			throw std::runtime_error("listen() 실패");
		}
		return;
	}
	ClientSocket ListenSockAccept(sockaddr_in* client_addr) {
		int addr_len = static_cast<int>(sizeof(*client_addr));
		SOCKET client_sock = accept(sock, (sockaddr*) client_addr, &addr_len);
		if (client_sock == INVALID_SOCKET) { // accept() 재실행은 catch문에서 continue를 호출해서 해결해야함
			throw std::runtime_error("accept 실패");
		}
		
		return ClientSocket(client_sock);
	}

	ListenSocket(const ListenSocket&) = delete;
	ListenSocket& operator=(const ListenSocket&) = delete;


	~ListenSocket() {
		if (sock != INVALID_SOCKET) {
			closesocket(sock);
		}
	}
};

class WinsockGuard {
public:
	WinsockGuard() {
		WSADATA wsa;
		int WSAStartupRes = WSAStartup(MAKEWORD(2, 2), &wsa);
		if (WSAStartupRes != 0) {
			throw std::runtime_error("윈속 초기화 실패");
		}
	}
	~WinsockGuard() {
		WSACleanup();
	}
};

class ClientManager {
private:
	// 여기에다가 ClientMagager 클래스가 관리할 공유 컨테이너 선언
	std::mutex client_mutex;
public:
	// 여기에다가 공유 컨테이너에 삽입, 삭제, 클라이언트 수 체크를 담당할 함수 선언
};

struct ClientInfo {
	ClientSocket sock;
	sockaddr_in addr;
};

const int SERVER_PORT = htons(9000);
const int PAYLOAD_SIZE = 4096;
const int BUFFER_SIZE = 4096;
const int HEADER_SIZE = 8;
const int HEADER_TYPE_SIZE = 4;
const int HEADER_LENGTH_SIZE = 4;

const int32_t HEADER_ERROR = 0;
const int32_t HEADER_SAFE = -1;

ClientManager Manager;

int client_thread(ClientInfo info) { // 지금은 단순 값 복사. 스마트 포인터 배우면 std::shared_ptr 사용해볼 예정.

}

int main() {
	try {
		WinsockGuard winsock;

		ListenSocket server_sock;
		sockaddr_in server_addr{};
		server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		server_addr.sin_family = AF_INET;
		server_addr.sin_port = SERVER_PORT;

		server_sock.ListenSockBind(&server_addr);

		server_sock.ListenSockListen();
		
		while (true) {
			sockaddr_in client_addr{};
			try {

				ClientInfo client{
				server_sock.ListenSockAccept(&client_addr),
				client_addr
				};

				
				std::thread ClientThread(client_thread, );

				ClientThread.detach();
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