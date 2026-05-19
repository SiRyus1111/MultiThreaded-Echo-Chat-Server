#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "Common.h" // err_quit, err_display
#include "NetCommon.h" // 기존의 에코 서버의 상태 관리 구조체, 헤더 구조체, 송 / 수신 함수
#include <thread>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include "socketRAII.h" // 기존의 에코 서버의 RAII 객체들
#include <atomic>
#include <algorithm>

#pragma comment(lib, "Ws2_32.lib")

// OOP 싫어... RAII 싫어.. 근데 왜 재밌냐 시발

const int SERVER_PORT = 9000;
const int PAYLOAD_SIZE = 4096;
const int BUFFER_SIZE = 4096;
const int HEADER_SIZE = 8;
const int HEADER_TYPE_SIZE = 4;
const int HEADER_LENGTH_SIZE = 4;

const char header_err_msg[] = "[SERVER]헤더의 최댓값 초과됨. 서버에서 연결을 종료합니다.\n";
uint32_t host_err_msg_len = static_cast<uint32_t>(strlen(header_err_msg));

class ClientSession;

class ClientManager : public std::enable_shared_from_this<ClientManager> {
private:
	std::vector<std::shared_ptr<ClientSession>> clients;
	std::mutex client_mutex;
public:
	ClientManager() {};
	~ClientManager() {};

	void AddClient(std::shared_ptr<ClientSession> client);

	void RemoveClient(std::shared_ptr<ClientSession> client);

	// ClientSession 객체 복사 방지용
	ClientManager& operator=(const ClientManager& c) = delete;
	ClientManager(const ClientManager&) = delete;
};

class ClientSession : public std::enable_shared_from_this<ClientSession> {
private:
	std::unique_ptr<ClientSocket> ClientSock;
	sockaddr_in ClientAddr;
	std::weak_ptr<ClientManager> Manager_wp;
	NetState ClientState; // 단순 값 복사
	std::atomic<bool> closing = false; // alignas(64) 가급적 필요할 듯. false sharing 고려해야함.
public:	
	ClientSession(std::unique_ptr<ClientSocket> s, sockaddr_in addr) : ClientSock(std::move(s)), ClientAddr(addr), ClientState{} { // move로 ClientSocket unique_ptr 객체 옮기기, addr 소켓 주소 구조체는 간단한 구조체이므로 단순 복사

	}

	enum class PacketType : int32_t {
		HEADER_ERROR,
		SAFE
	};

	

	// share_from_this()로 받기 / ClientManager 객체에서 사용하는 함수
	void AddToManager(std::shared_ptr<ClientManager> Manager_sp) {
		Manager_wp = Manager_sp;
		return;
	}

	// 송수신 로직이 구현되어있는 함수
	void Run();

	int SendPacket(const char* msg, PacketType type);

	int RecvPacket(char* buf);
	
	void RemoveThisClient() {
		if (auto locked = Manager_wp.lock()) {
			locked->RemoveClient(shared_from_this());
		}
		else {
			std::cout << "ClientManager 객체 이미 소멸됨. RemoveClient()가 실행되지 않습니다.\n";
		}
	}

	// ClientSession 객체 복사 방지용
	ClientSession& operator=(const ClientSession& c) = delete;
	ClientSession(const ClientSession&) = delete;

};

void ClientManager::AddClient(std::shared_ptr<ClientSession> client) {

	// 여기에다가 공유 컨테이너에 삽입, 삭제, 클라이언트 수 체크를 담당할 함수 선언
	client->AddToManager(shared_from_this());

	std::lock_guard<std::mutex> lock(client_mutex); // lock_guard로 스코프 벗어나면 락 해제. 공유 컨테이너에 접근하는 모든 함수에 자동으로 이걸 적용하는게 좋아보임. 반복 줄이기.
	clients.push_back(client);

	return;
};

void ClientManager::RemoveClient(std::shared_ptr<ClientSession> client) {
	// 여기에 clients에서 해당 shared_ptr의 client만 지우는 로직의 코드
	std::lock_guard<std::mutex> lock(client_mutex);
	clients.erase(std::remove(clients.begin(), clients.end(), client), clients.end());

}

void ClientSession::Run() {
	char buf[BUFFER_SIZE + 1];
	char addr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &ClientAddr.sin_addr, addr, sizeof(addr));

	while (true) {

		PacketHeader recv_net_header{};

		ClientState.header_recv = true;
		int header_recv_res = ClientSock->ClientSockRecv(ClientState, (char*)&recv_net_header, sizeof(PacketHeader));

		if (header_recv_res == SOCKET_ERROR || header_recv_res == 0) {
			break;
		}
		ClientState.header_recv = false;

		PacketHeader recv_host_header{};
		recv_host_header.type = ntohl(recv_net_header.type);
		recv_host_header.length = ntohl(recv_net_header.length);

		if (recv_host_header.length > BUFFER_SIZE) {
			ClientState.if_header_error = true;
			break;
		}

		ClientState.payload_recv = true;
		int payload_recv_res = ClientSock->ClientSockRecv(ClientState, (char*)buf, recv_host_header.length);

		if (payload_recv_res == SOCKET_ERROR || payload_recv_res == 0) {
			break;
		}
		ClientState.payload_recv = false;

		buf[payload_recv_res] = '\0';

		std::cout << "송신한 클라이언트 : IP 주소 = " << addr << " 포트 번호 = " << ntohs(ClientAddr.sin_port) << '\n';
		std::cout << "받은 총 바이트 수 : " << payload_recv_res + header_recv_res << " 받은 메시지 : " << buf << '\n';

		if (recv_host_header.type == HEADER_ERROR) {
			ClientState.if_peer_error = true;
			break;
		}

		PacketHeader send_net_header{};
		send_net_header.length = htonl(payload_recv_res);
		send_net_header.type = htonl(SAFE);

		ClientState.header_send = true;
		int header_send_res = ClientSock->ClientSockSend(ClientState, (char*)&send_net_header, sizeof(PacketHeader)); // PacketHeader 구조체에는 패딩 없음.

		if (header_send_res == SOCKET_ERROR) {
			break;
		}
		ClientState.header_send = false;

		ClientState.payload_send = true;
		int payload_send_res = ClientSock->ClientSockSend(ClientState, buf, recv_host_header.length);

		if (payload_send_res == SOCKET_ERROR) {
			break;
		}
		ClientState.payload_send = false;

	}
	closing.store(true);
	if (ClientState.if_error) {
		std::cout << "클라이언트와의 통신 과정에서 오류 발생 : ";

		if (ClientState.header_recv) std::cout << "헤더 수신 과정에서 오류 발생\n";

		else if (ClientState.payload_recv) std::cout << "페이로드 수신 과정에서 오류 발생\n";

		else if (ClientState.header_send) std::cout << "헤더 송신 과정에서 오류 발생\n";

		else if (ClientState.payload_send) std::cout << "페이로드 송신 과정에서 오류 발생\n";

	}
	else if (ClientState.if_header_error) {

		std::cout << "헤더의 값이 4096을 초과. 페이로드 수신 불가.\n";

		PacketHeader protocol_err_header;
		protocol_err_header.type = htonl(HEADER_ERROR);
		protocol_err_header.length = htonl(host_err_msg_len);

		int header_err_send_res = ClientSock->ClientSockSend(ClientState, (char*)&protocol_err_header, HEADER_SIZE);
		if (header_err_send_res == SOCKET_ERROR) {
			std::cout << "헤더 오류 메시지 클라이언트에 전송 실패.\n";
		}
		else {
			int err_send_res = ClientSock->ClientSockSend(ClientState, header_err_msg, host_err_msg_len);
			if (err_send_res == SOCKET_ERROR) {
				std::cout << "헤더 오류 메시지 클라이언트에 전송 실패.\n";
			}
		}
	}
	else if (ClientState.if_peer_error) {
		std::cout << "클라이언트에 보낸 헤더의 오류 수신.\n";
	}
	else if (ClientState.if_peer_exit) {
		std::cout << "클라이언트에서 연결을 종료하였습니다.\n";
	}

	// 여기에 RemoveClient() - clients에서 해당 ClientSession 제거
	return;
}

int ClientSession::SendPacket(const char* msg, PacketType type) {
	
}

int ClientSession::RecvPacket(char* buf) {

}

auto manager = std::make_shared<ClientManager>();

void client_thread(std::shared_ptr<ClientSession> session) { // detach()로 분리한 스레드
	session->Run();

	return; 
}

int main() {
	try {
		WinsockGuard winsock;

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

				auto client_session = std::make_shared<ClientSession>(std::move(client_socket), client_addr); // unique_ptr 배운 후 수정 예정

				manager->AddClient(client_session);

				std::thread ClientThread(client_thread, client_session);

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