#include <iostream>
#include <ws2tcpip.h>
#include <winsock2.h>
#include "Common.h" // err_quit, err_display
#include "NetCommon.h" // 기존의 에코 서버의 상태 관리 구조체, 헤더 구조체, 송 / 수신 함수
#include <thread>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include "socketRAII.h"

#pragma comment(lib, "Ws2_32.lib")

// OOP 싫어... RAII 싫어.. 근데 왜 재밌냐 시발

class ClientManager;

class ClientSession {
private:
	std::unique_ptr<ClientSocket> ClientSock;
	sockaddr_in ClientAddr;
	std::weak_ptr<ClientManager> Manager_wp;
public:	
	ClientSession(std::unique_ptr<ClientSocket> s, sockaddr_in addr) : ClientSock(std::move(s)), ClientAddr(addr) { // move로 ClientSocket 객체 옮기기, addr은 모르겠다.. 일단 오늘은 단순히 복사하기로 하자.. 피곤해.

	}

	// share_from_this()로 받기 / ClientManager 객체에서 사용하는 함수
	void AddToManager(std::shared_ptr<ClientManager> Manager_sp) {
		Manager_wp = Manager_sp;

		return;
	}

	// ClientSession 객체 복사 방지용
	ClientSession& operator=(const ClientSession& c) = delete;
	ClientSession(const ClientSession&) = delete;

	~ClientSession() {
		if (std::shared_ptr<ClientManager> locked = Manager_wp.lock()) {
			// 여기서 ClientManager의 공유 컨테이너에서 해당 클라이언트 제거하는 함수 호출?
		}
	}
};

class ClientManager : public std::enable_shared_from_this<ClientManager> {
private:
	std::vector<std::shared_ptr<ClientSession>> clients;
	std::mutex client_mutex;
public:
	ClientManager() {};
	~ClientManager() {};

	// ClientSession 객체 복사 방지용
	ClientManager& operator=(const ClientManager& c) = delete;
	ClientManager(const ClientManager&) = delete;

	// 여기에다가 공유 컨테이너에 삽입, 삭제, 클라이언트 수 체크를 담당할 함수 선언
	void AddClient(std::shared_ptr<ClientSession> client) {
		std::lock_guard<std::mutex> lock(client_mutex); // lock_guard로 스코프 벗어나면 락 해제. 공유 컨테이너에 접근하는 모든 함수에 자동으로 이걸 적용하는게 좋아보임. 반복 줄이기.

		clients.push_back(client);
		client->AddToManager(shared_from_this());

		return;
	}
};

const int SERVER_PORT = htons(9000);
const int PAYLOAD_SIZE = 4096;
const int BUFFER_SIZE = 4096;
const int HEADER_SIZE = 8;
const int HEADER_TYPE_SIZE = 4;
const int HEADER_LENGTH_SIZE = 4;

std::shared_ptr<ClientManager> manager = std::make_shared<ClientManager>();

int client_thread(ClientSession session) { // 지금은 단순 값 복사. 스마트 포인터 배우면 std::shared_ptr / std::unique_ptr 사용해볼 예정.

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

				std::unique_ptr<ClientSocket> client_socket = std::make_unique<ClientSocket>(server_sock.ListenSockAccept(&client_addr));

				ClientSession client(std::move(client_socket), client_addr); // unique_ptr 배운 후 수정 예정

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