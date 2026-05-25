#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "Common.h" // err_quit, err_display
#include "NetCommon.h" // 기존의 에코 서버의 상태 관리 구조체, 헤더 구조체, 송 / 수신 함수
#include <thread>
#include <vector>
#include <memory>
#include <map>
#include <unordered_map>
#include <mutex>
#include "socketRAII.h" // 기존의 에코 서버의 RAII 객체들
#include <atomic>
#include <algorithm>
#include "LineLogger.h"

#pragma comment(lib, "Ws2_32.lib")

// OOP 싫어... RAII 싫어.. 근데 왜 재밌냐 시발

using SessionID = uint64_t;

const int SERVER_PORT = 9000;
const int PAYLOAD_SIZE = 4096;
const int BUFFER_SIZE = PAYLOAD_SIZE + 1; // \0을 맨 끝에 추가해야하므로
const int HEADER_SIZE = 8;
const int HEADER_TYPE_SIZE = 4;
const int HEADER_LENGTH_SIZE = 4;
const SessionID INITIAL_SESSION_ID = 0;

const char header_err_msg[] = "The maximum value of the header's length field has been exceeded. The server is terminating the connection.";
uint32_t host_err_msg_len = static_cast<uint32_t>(strlen(header_err_msg));

class ClientSession;

class ClientManager : public std::enable_shared_from_this<ClientManager> {
private:
	std::unordered_map<SessionID, std::shared_ptr<ClientSession>> clients;
	std::mutex clients_mutex;
public:
	ClientManager() {};
	~ClientManager() {};

	void AddClient(std::shared_ptr<ClientSession> client, SessionID id);

	void RemoveClient(SessionID id);

	// ClientSession 객체 복사 방지용
	ClientManager& operator=(const ClientManager& c) = delete;
	ClientManager(const ClientManager&) = delete;
};

class ClientSession : public std::enable_shared_from_this<ClientSession> {
private:
	std::unique_ptr<ClientSocket> ClientSock;
	sockaddr_in ClientAddr;
	char ClientAddrStr[INET_ADDRSTRLEN];
	std::weak_ptr<ClientManager> Manager_wp;
	NetState ClientState; // 단순 값 복사
	std::atomic<bool> closing = false; // alignas(64) 가급적 필요할 듯. false sharing 고려해야함.
	SessionID session_id;
public:	
	ClientSession(std::unique_ptr<ClientSocket> s, sockaddr_in addr, SessionID id) 
		: ClientSock(std::move(s)), 
		ClientAddr(addr), 
		ClientAddrStr{}, 
		ClientState{}, 
		closing(false), 
		session_id(id) { // move로 ClientSocket unique_ptr 객체 옮기기, addr 소켓 주소 구조체는 간단한 구조체이므로 단순 복사
		inet_ntop(AF_INET, &ClientAddr.sin_addr, ClientAddrStr, sizeof(ClientAddrStr));

		LineLogger::GetInstance().WriteSessionLog(session_id, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::CONNECTED, "Client Connected.");
	}

	void TransportExceptionHandling(NetState State) {

		if (State.if_error) {

			if (State.header_recv) LineLogger::GetInstance().WriteSessionLog(session_id, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::TRANSPORT_ERROR, "Transport Error occured during Header Receiving.");

			else if (State.payload_recv) LineLogger::GetInstance().WriteSessionLog(session_id, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::TRANSPORT_ERROR, "Transport Error occured during Payload Receiving.");

			else if (State.header_send) LineLogger::GetInstance().WriteSessionLog(session_id, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::TRANSPORT_ERROR, "Transport Error occured during Header Sending.");

			else if (State.payload_send) LineLogger::GetInstance().WriteSessionLog(session_id, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::TRANSPORT_ERROR, "Transport Error occured during Payload Sending.");

		}
		else if (State.if_header_error) {

			PacketHeader protocol_err_header;
			protocol_err_header.type = htonl(static_cast<int32_t>(PacketType::HEADER_ERROR));
			protocol_err_header.length = htonl(host_err_msg_len);

			LineLogger::GetInstance().WriteSessionLog(session_id, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::SEND_ERROR_PACKET, "Transport Error occured during Payload Sending.");
			NetState header_err_send_res = SendPacket(header_err_msg, host_err_msg_len, PacketType::HEADER_ERROR);
			if (!(!header_err_send_res.if_error && !header_err_send_res.if_peer_exit && !header_err_send_res.if_header_error && !header_err_send_res.if_peer_error)) { // 드 모르간 적용해서 하나라도 false라면 AND 연산을 더이상 하지 않는 것을 이용해서 최적화. 
				// 이 함수가 호출될 때는 if_header_error 플래그가 true가 될 수 없음. 
				// send() 할 때도 recv()가 정상적으로 실행된 경우에만 해당 플래그가 true로 바뀔 수 있기 떄문에
				// 현재 구조에서는 상대의 패킷을 recv() 했을 때만 해당 플래그가 true로 바뀔 수 있음.
				// 이 함수는 호출될 때에 send() 한 후에 오류가 났을 때 결과를 출력함.
				TransportExceptionHandling(header_err_send_res); 
			}
		}
		else if (State.if_peer_error) {
			LineLogger::GetInstance().WriteSessionLog(session_id, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::RECEIVE_ERROR_PACKET, "Received an error packet from a Client.");
		}
		else if (State.if_peer_exit) {
			LineLogger::GetInstance().WriteSessionLog(session_id, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::DISCONNECTED, "The client has successfully closed the connection.");
		}

		RemoveThisClient();

		return;
	}

	// share_from_this()로 받기 / ClientManager 객체에서 사용하는 함수
	void AddToManager(std::shared_ptr<ClientManager> Manager_sp) {
		Manager_wp = Manager_sp;
		return;
	}

	// 송수신 로직이 구현되어있는 함수
	void Run();

	NetState SendPacket(const char* msg, uint32_t len, PacketType type);

	NetState RecvPacket(char* buf);

	void MarkClosing() {
		closing.store(true);
	}
	
	void RemoveThisClient() {
		if (auto locked = Manager_wp.lock()) {
			locked->RemoveClient(session_id);
		}
		else {
			std::cout << "ClientManager 객체 이미 소멸됨. RemoveClient()가 실행되지 않습니다.\n";
		}
	}

	// ClientSession 객체 복사 방지용
	ClientSession& operator=(const ClientSession& c) = delete;
	ClientSession(const ClientSession&) = delete;

};

void ClientManager::AddClient(std::shared_ptr<ClientSession> client, SessionID id) {

	// ClientSession 객체에 shared_ptr을 넘겨줘서 ClientSession 객체의 weak_ptr을 초기화.
	client->AddToManager(shared_from_this());

	std::lock_guard<std::mutex> lock(clients_mutex); // 락을 최대한 짧게 잡고있기. DeadLock 위험 감소, Lock Contention 감소.
	clients[id] = client;

	return;
};

void ClientManager::RemoveClient(SessionID id) {
	// 여기에 clients에서 해당 SessionID의 ClientSession만 지우는 로직의 코드
	std::lock_guard<std::mutex> lock(clients_mutex);
	clients.erase(id); 
}

void ClientSession::Run() {
	char buf[BUFFER_SIZE + 1];

	while (true) {

		NetState recv_state = RecvPacket(buf);

		if (recv_state.if_error ||
			recv_state.if_header_error ||
			recv_state.if_peer_error ||
			recv_state.if_peer_exit) {
			MarkClosing();
			TransportExceptionHandling(recv_state);

			break;
		}

		NetState send_state = SendPacket(buf, (uint32_t)strlen(buf), PacketType::SAFE);

		if (send_state.if_error ||
			send_state.if_header_error ||
			send_state.if_peer_error ||
			send_state.if_peer_exit) {
			MarkClosing();
			TransportExceptionHandling(send_state);

			break;
		}

	}

	return;
}

// msg는 반드시 BUFFER_SIZE 이내의 크기여야 한다. len은 메시지의 크기를 나타낸다.
NetState ClientSession::SendPacket(const char* msg, uint32_t len, PacketType type) {

	// 메시지 길이 검사
	if (len > PAYLOAD_SIZE) {
		ClientState.if_header_error = true;
		return ClientState;
	}

	// 헤더 송신
	PacketHeader send_net_header{};
	send_net_header.length = htonl(len);
	send_net_header.type = htonl(static_cast<int32_t>(type));

	ClientState.header_send = true;
	int header_send_res = ClientSock->ClientSockSend(ClientState, (char*)&send_net_header, sizeof(PacketHeader)); // 해당 함수 내에서 transport error나 peer exit는 기록됨

	if (header_send_res == SOCKET_ERROR) {
		return ClientState;
	}
	ClientState.header_send = false;

	// 페이로드 송신
	ClientState.payload_send = true;
	int payload_send_res = ClientSock->ClientSockSend(ClientState, msg, len);

	if (payload_send_res == SOCKET_ERROR) {
		return ClientState;
	}
	ClientState.payload_send = false;

	LineLogger::GetInstance().WriteSessionLog(session_id, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::SEND_COMPLETE, "Message Sent : ", msg);

	return ClientState;
}

// buf는 반드시 BUFFER_SIZE 크기여야 한다.
NetState ClientSession::RecvPacket(char* buf) {

	// 헤더 수신
	PacketHeader recv_net_header{};

	ClientState.header_recv = true;
	int header_recv_res = ClientSock->ClientSockRecv(ClientState, (char*)&recv_net_header, sizeof(PacketHeader));

	if (header_recv_res == SOCKET_ERROR || header_recv_res == 0) {
		return ClientState;
	}
	ClientState.header_recv = false;

	PacketHeader recv_host_header{};
	recv_host_header.type = ntohl(static_cast<int32_t>(recv_net_header.type));
	recv_host_header.length = ntohl(recv_net_header.length);

	if (recv_host_header.length > PAYLOAD_SIZE || recv_host_header.length == 0) { // length == 0이어도 protocol error로 처리
		ClientState.if_header_error = true;
		return ClientState;
	}

	if (recv_host_header.type != static_cast<int32_t>(PacketType::SAFE) &&
		recv_host_header.type != static_cast<int32_t>(PacketType::HEADER_ERROR)) {
		ClientState.if_header_error = true;
		return ClientState;
	}

	// 페이로드 수신
	ClientState.payload_recv = true;
	int payload_recv_res = ClientSock->ClientSockRecv(ClientState, (char*)buf, recv_host_header.length);

	if (payload_recv_res == SOCKET_ERROR || payload_recv_res == 0) {
		return ClientState;
	}
	ClientState.payload_recv = false;

	buf[recv_host_header.length] = '\0';

	if (recv_host_header.type == static_cast<int32_t>(PacketType::HEADER_ERROR)) {
		ClientState.if_peer_error = true;
		return ClientState;
	}

	LineLogger::GetInstance().WriteSessionLog(session_id, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::RECV_COMPLETE, "Message Received : ", buf);

	return ClientState;
}



void client_thread(std::shared_ptr<ClientSession> session) { // detach()로 분리한 스레드
	session->Run();

	return; 
}

int main() {
	try {
		WinsockGuard winsock;

		auto manager = std::make_shared<ClientManager>();
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