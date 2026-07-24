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
#include <sstream>

#pragma comment(lib, "Ws2_32.lib")

// OOP 싫어... RAII 싫어.. 근데 왜 재밌냐 시발

using SessionID = uint64_t;
using Nickname = std::string;

const int SERVER_PORT = 9000;
const int PAYLOAD_SIZE = 4096;
const int BUFFER_SIZE = PAYLOAD_SIZE + 1; // \0을 맨 끝에 추가해야하므로
const int HEADER_TYPE_SIZE = 4;
const int HEADER_LENGTH_SIZE = 4;
const int HEADER_NICKNAME_SIZE = 32; // 헤더의 닉네임 필드 크기
const int HEADER_SIZE = HEADER_TYPE_SIZE + HEADER_LENGTH_SIZE + HEADER_NICKNAME_SIZE;

const SessionID INITIAL_SESSION_ID = 0;
const size_t MAX_NICKNAME_LENGTH = 32; // 가능한 닉네임 최대 길이(HEADER_NICKNAME_SIZE와 의미가 다름)
const Nickname ECHO_NICK = "EchoFromServer";
const Nickname SERVER_NICK = "ServerMessage";

const char header_err_msg[] = "The header is invalid. The server is terminating the connection.";
uint32_t host_err_msg_len = static_cast<uint32_t>(strlen(header_err_msg));

const std::string nick_already_used_msg = "That nickname is already taken. Please enter a different nickname.";
const std::string nick_change_sucess_msg = "Your nickname has been successfully changed.";
const std::string nick_length_exceed = "The maximum length for the nickname has been exceeded.";

struct Packet {
	PacketHeader header{};
	std::unique_ptr<std::string> payload_up; // 포인터인거 까먹을까봐 이름 이렇게 함

	Packet() : payload_up(std::make_unique<std::string>()) { // 생성자에서 std::string 객체도 생성

	}
};

struct RecvResult {
	NetState state{}; // 수신 과정에서 발생한 상태
	std::shared_ptr<Packet> packet; // 수신한 패킷
};

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

	// ClientSession에서 ClientManager::clients를 얻을 필요가 있을 때 사용하기 위한 함수
	std::unordered_map<SessionID, std::shared_ptr<ClientSession>> GetClients() {
		std::lock_guard<std::mutex> lock(clients_mutex);
		return clients;
	}

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
	Nickname nickname;
public:	
	ClientSession(std::unique_ptr<ClientSocket> s, sockaddr_in addr, SessionID id) 
		: ClientSock(std::move(s)), 
		ClientAddr(addr), 
		ClientAddrStr{}, 
		ClientState{}, 
		closing(false), 
		session_id(id) { // move로 ClientSocket unique_ptr 객체 옮기기, addr 소켓 주소 구조체는 간단한 구조체이므로 단순 복사
		inet_ntop(AF_INET, &ClientAddr.sin_addr, ClientAddrStr, sizeof(ClientAddrStr));

		// 기본 닉네임 설정(임시 코드)
		std::ostringstream oss;
		oss << "user_" << session_id;
		nickname = oss.str();
		LineLogger::GetInstance().WriteSessionLog(session_id, nickname,ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::CONNECTED, "Client Connected.");
	}

	NetState GetState() const {
		return ClientState;
	}

	bool GetClosing() const {
		return static_cast<bool>(closing);
	}

	SessionID GetSessionID() const {
		return session_id;
	}

	Nickname GetNickname() const {
		return nickname;
	}

	sockaddr_in GetBinaryAddr() const {
		return ClientAddr;
	}

	std::string GetStrAddr() const {
		return std::string(ClientAddrStr, strlen(ClientAddrStr));
	}

	void HandleRecvPacket(std::shared_ptr<Packet> packet) { // 이거 RecvResult가 아닌 Packet 기반으로 수정해야함
		bool quit = false;

		switch (static_cast<PacketType>(ntohl(packet->header.type))) {
	    	case PacketType::CHAT_MESSAGE: // 여기는 SendPacket() 함수 그대로 쓰는걸로 일단..
    		{

		    	// 수신한 패킷의 타입이 CHAT_MESSAGE일 때 실행할 코드
	    		// 이 함수 부분은 HandlePacket() 안으로 넣어야할 듯.. (넣었음)

				// 패킷이 공개된 후에는 수정할 수 없다는 불변식 위반 아님. 애초에 패킷이 공개가 안되고 해당 ClientSession 내부에서 처리됨.
				memset(&packet->header.nickname, '\0', HEADER_NICKNAME_SIZE); // 패딩 채우기
				memcpy(&packet->header.nickname, ECHO_NICK.c_str(), ECHO_NICK.size()); // 메모리 카피로 문자열 바이트 그대로 헤더의 닉네임 필드에 넣어버리기


    			NetState send_state = SendPacket(packet); // 여기 (uint32_t) strlen(buf)에서 res.length로 수정함. 만약 보내려는 문자 중간에 널문자 있으면 클남.

			    if (send_state.transport_error ||
				    send_state.protocol_error ||
			    	send_state.peer_closed) {
		    		
	    			HandleTransportException(send_state);

    			}

			    break;
		    }
			case PacketType::HEADER_ERROR:
			{
				// 수신한 패킷의 타입이 HEADER_ERROR일 때 실행할 코드
				LineLogger::GetInstance().WriteSessionLog(session_id,nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::RECEIVE_ERROR_PACKET, "Received an error packet from a Client.");
			    // 여기 수정 필요함. 꼭 기억해두셈. 여기 RemoveThisClient() 함수 없음. CAS 기반 MarkClosing() 함수 추가할 때 이거 추가하셈.
				// 수정 완료
				if (TryMarkClosing()) {
					RemoveThisClient();
				}

				break;
			}
			case PacketType::NICKNAME_CHANGE:
			{
				if (ntohl(packet->header.length) > MAX_NICKNAME_LENGTH) {
					// 닉네임 설정 실패 시 정책에 맞게 실패한 이유를 페이로드에 실어서 보냄
					packet->header.type = htonl(static_cast<int32_t>(PacketType::NICKNAME_CHANGE_FAILED));
					memset(&packet->header.nickname, '\0', HEADER_NICKNAME_SIZE); // 패딩 채우기
					memcpy(&packet->header.nickname, SERVER_NICK.c_str(), SERVER_NICK.size());
					*packet->payload_up = nick_length_exceed;
					packet->header.length = htonl(nick_length_exceed.size());

					NetState send_res = SendPacket(packet);

					if (!(!send_res.transport_error && !send_res.peer_closed && !send_res.protocol_error)) {
						HandleTransportException(send_res);
					}

					// break하는 코드 추가
				}

				bool nick_already_used = false;
				std::unordered_map<SessionID, std::shared_ptr<ClientSession>> snapshot;

				if (auto locked = Manager_wp.lock()) {
					snapshot = locked->GetClients();
				}

				for (auto pair : snapshot) {
					if (*packet->payload_up == pair.second->nickname) { // 이거 버그났었음. 
						nick_already_used = true;
						break;
					}
				}

				if (nick_already_used) {
					// 닉네임 설정 실패 시 정책에 맞게 실패한 이유를 페이로드에 실어서 보냄
					packet->header.type = htonl(static_cast<int32_t>(PacketType::NICKNAME_CHANGE_FAILED));
					memset(&packet->header.nickname, '\0', HEADER_NICKNAME_SIZE); // 패딩 채우기
					memcpy(&packet->header.nickname, SERVER_NICK.c_str(), SERVER_NICK.size());
					*packet->payload_up = nick_already_used_msg;
					packet->header.length = htonl(nick_already_used_msg.size());

					NetState send_res = SendPacket(packet);

					if (!(!send_res.transport_error && !send_res.peer_closed && !send_res.protocol_error)) {
						HandleTransportException(send_res);
					}

					break;
				}
				// 이 이후로는 유효한 닉네임인 경우에만 실행될 수 있음

				nickname = *packet->payload_up;

				// 닉네임 설정 성공 시 클라이언트의 지역 닉네임을 갱신하기 위해 클라이언트가 갱신할 닉네임을 페이로드에 실어서 보내고,
				// 닉네임 설정 성공 메시지는 전적으로 클라이언트에게 책임을 맏김
				packet->header.type = htonl(static_cast<int32_t>(PacketType::NICKNAME_CHANGE_SUCESS));
				memset(&packet->header.nickname, '\0', HEADER_NICKNAME_SIZE); // 패딩 채우기
				memcpy(&packet->header.nickname, SERVER_NICK.c_str(), SERVER_NICK.size());
				// 페이로드는 변경할 닉네임. 그래서 그대로여도 됨.

				NetState send_res = SendPacket(packet);

				if (!(!send_res.transport_error && !send_res.peer_closed && !send_res.protocol_error)) {
					HandleTransportException(send_res);
				}

				break;
			}
			// 유효하지 않은 패킷 타입은 이미 RecvPacket() / HandleTransportException()에서 판별 및 처리해줌
		}
		
	}

	void HandleTransportException(NetState State) {
		// MarkClosing 시도하고 실패하면 다른 스레드가 예외 처리 한다는 뜻이니까 해당 스레드는 예외처리 하지 않음.
		if (!TryMarkClosing()) {
			return;
		}

		if (State.transport_error) {

			if (State.header_recv) LineLogger::GetInstance().WriteSessionLog(session_id, nickname,ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::TRANSPORT_ERROR, "Transport Error occured during Header Receiving.");

			else if (State.payload_recv) LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::TRANSPORT_ERROR, "Transport Error occured during Payload Receiving.");

			else if (State.header_send) LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::TRANSPORT_ERROR, "Transport Error occured during Header Sending.");

			else if (State.payload_send) LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::TRANSPORT_ERROR, "Transport Error occured during Payload Sending.");

		}
		else if (State.protocol_error) {

			std::shared_ptr<Packet> packet = std::make_shared<Packet>();

			packet->header.type = htonl(static_cast<int32_t>(PacketType::HEADER_ERROR));
			memset(&packet->header.nickname, '\0', HEADER_NICKNAME_SIZE); // 패딩 채우기
			memcpy(&packet->header.nickname, SERVER_NICK.c_str(), SERVER_NICK.size());
			*packet->payload_up = header_err_msg;
			packet->header.length = htonl(strlen(header_err_msg));

			LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::SEND_ERROR_PACKET, "Sending an error packet...");

			NetState header_err_send_res = SendPacket(packet);

			if (!(!header_err_send_res.transport_error && !header_err_send_res.peer_closed && !header_err_send_res.protocol_error && !header_err_send_res.peer_protocol_error)) { // 드 모르간 적용해서 하나라도 false라면 AND 연산을 더이상 하지 않는 것을 이용해서 최적화. 
				// 이 함수가 호출될 때는 protocol_error 플래그가 true가 될 수 없음. 
				// send() 할 때도 recv()가 정상적으로 실행된 경우에만 해당 플래그가 true로 바뀔 수 있기 떄문에
				// 현재 구조에서는 상대의 패킷을 recv() 했을 때만 해당 플래그가 true로 바뀔 수 있음.
				// 이 함수는 호출될 때에 send() 한 후에 오류가 났을 때 결과를 출력함.
				HandleTransportException(header_err_send_res);
			}
		}
		/*
		else if (State.peer_protocol_error) { // 이거 뺴버리자. 따로 HEADER_ERROR인 패킷을 받았을 때 처리하는 함수를 만들어야겠다..
			LineLogger::GetInstance().WriteSessionLog(session_id, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::RECEIVE_ERROR_PACKET, "Received an error packet from a Client.");
		}
		*/
		else if (State.peer_closed) {
			LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::DISCONNECTED, "The client has successfully closed the connection.");
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

	NetState SendPacket(std::shared_ptr<Packet> packet); // 이 함수는 좀 후순위로 수정

	RecvResult RecvPacket(); // 이 함수 우선 수정

	bool TryMarkClosing() {
		bool expected = false;

		if (!closing.compare_exchange_strong(expected, true)) {
			return false;
		}

		return true;
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

		RecvResult recv_res = RecvPacket();

		if (recv_res.state.transport_error ||
			recv_res.state.protocol_error ||
			recv_res.state.peer_protocol_error || // 이건 일단 남겨놓음. 추후에 NetState 구조체 갈아엎으면서 수정할 예정
			recv_res.state.peer_closed) {
			HandleTransportException(recv_res.state);

			break;
		}

		// 대충 여기다가 HandleRecvPacket() 함수 (아직 HandlePacket() 함수는 미구현이기에 주석만 남겨놓음)
		HandleRecvPacket(std::move(recv_res.packet));

		// 여기에다 closing == true 체크 추가
		if (closing == true) {
			break;
		}

	}

	return;
}

NetState ClientSession::SendPacket(std::shared_ptr<Packet> packet) {

	NetState send_packet_state{};

	PacketHeader send_host_header{};

	send_host_header.length = ntohl(packet->header.length);
	send_host_header.type = ntohl(packet->header.type);

	// 메시지 길이 검사
	if (send_host_header.length > PAYLOAD_SIZE || send_host_header.length == 0) {
		ClientState.protocol_error = true;
		send_packet_state.protocol_error = true;
		return send_packet_state;
	}

	// 패킷 타입 유효성 검사
	PacketType send_packet_type = static_cast<PacketType>(ntohl(packet->header.type));
	
	if (send_packet_type != PacketType::CHAT_MESSAGE &&
		send_packet_type != PacketType::HEADER_ERROR &&
		send_packet_type != PacketType::NICKNAME_CHANGE &&
		send_packet_type != PacketType::NICKNAME_CHANGE_FAILED &&
		send_packet_type != PacketType::NICKNAME_CHANGE_SUCESS){
		ClientState.protocol_error = true;
		send_packet_state.protocol_error = true;

		return send_packet_state;
	}

	/*
	// 닉네임 길이 검사(혹시 모르니)
	if (nick.size() > 32) {
		ClientState.protocol_error = true;
		send_packet_state.protocol_error = true;

		return send_packet_state;
	}
	*/
    // Packet 구조체 기반 송신에는 할 필요가 있나 싶음..

	// 헤더 송신
	/*
	PacketHeader send_net_header{};
	send_net_header.length = htonl(len);
	send_net_header.type = htonl(static_cast<int32_t>(type));
	*/

	// 이 코드에 대한 자세한 내용은 닉네임 시스템 설계 문서 - `PacketHeader::nickname`필드를 설정하는 과정 파트 참조
	// 주석으로 설명하기엔 너무 길다..
	// 결국 이건 닉네임 길이가 가변적이기 때문에 헤더에서 닉네임을 표시하지 않는 바이트는 '\0'으로 패딩 처리하는 코드라 볼 수 있음.
	/*
	char nick_buf[MAX_NICKNAME_LENGTH];
	memset(nick_buf, '\0', MAX_NICKNAME_LENGTH);
	memcpy(nick_buf, nick.c_str(), nick.size());
	memcpy(send_net_header.nickname, nick_buf, HEADER_NICKNAME_SIZE); // char*(c스타일 문자열) 이므로 바이트 정렬 신경쓸 필요 없음
	*/
	// 이 과정은 따로 헤더의 닉네임 필드가 필요할 때 할 예정

	ClientState.header_send = true;
	send_packet_state.header_send = true;
	int header_send_res = ClientSock->ClientSockSend(ClientState, (char*)&packet->header, sizeof(PacketHeader)); // 해당 함수 내에서 transport error나 peer exit는 기록됨

	if (header_send_res == SOCKET_ERROR) {
		send_packet_state.transport_error = true;
		return send_packet_state;
	}
	ClientState.header_send = false;
	send_packet_state.header_send = false;

	// 페이로드 송신
	ClientState.payload_send = true;
	send_packet_state.payload_send = true;
	int payload_send_res = ClientSock->ClientSockSend(ClientState, packet->payload_up->c_str(), ntohl(packet->header.length));

	if (payload_send_res == SOCKET_ERROR) {
		send_packet_state.transport_error = true;
		return send_packet_state;
	}
	ClientState.payload_send = false;
	send_packet_state.payload_send = false;

	// 수정 : 무조건 메시지만 송신한게 아니라 패킷을 송신했다는 것을 나타내기 위해서 Message Sent : msg가 아닌 Packet Sent.로 로그 메시지 수정
	LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::SEND_COMPLETE, "Packet Sent.");

	return send_packet_state;
}

RecvResult ClientSession::RecvPacket() {

	NetState recv_packet_state{};
	RecvResult result{};
	std::shared_ptr<Packet> packet = std::make_shared<Packet>();

	// 헤더 수신

	ClientState.header_recv = true;
	recv_packet_state.header_recv = true;
	int header_recv_res = ClientSock->ClientSockRecv(ClientState,(char*) &packet->header, sizeof(PacketHeader));

	if (header_recv_res == SOCKET_ERROR) {
		recv_packet_state.transport_error = true;
		result.state = recv_packet_state;

		return result;
	}
	if (header_recv_res == 0) {
		recv_packet_state.peer_closed = true;
		result.state = recv_packet_state;

		return result;
	}
	ClientState.header_recv = false;
	recv_packet_state.header_recv = false;

	PacketHeader recv_host_header{};
	recv_host_header.type = ntohl(static_cast<int32_t>(packet->header.type));
	recv_host_header.length = ntohl(packet->header.length);

	/*
	// 이것도 닉네임 시스템 설계 문서 참조
	char nick_buf[MAX_NICKNAME_LENGTH + 1]; // 32바이트짜리 닉네임일 경우에도 문자열로 인식하기 위해서 맨 끝에 널문자를 붙이기 위해 +1
	memcpy(nick_buf, recv_net_header.nickname, HEADER_NICKNAME_SIZE);
	nick_buf[MAX_NICKNAME_LENGTH] = '\0'; // 32바이트짜리 닉네임일 경우에도 문자열로 읽을 수 있게 맨 끝에 널문자 붙임. 32바이트보다 닉네임을 표현하는 바이트 수가 적더라도 이미 그 빈 바이트들은 '\0'으로 처리되어있어서 문제 없음
	*/

	if (recv_host_header.length > PAYLOAD_SIZE || recv_host_header.length == 0) { // length == 0이어도 protocol error로 처리
		ClientState.protocol_error = true;
		recv_packet_state.protocol_error = true;
		result.state = recv_packet_state;
		
		return result;
	}

	// NICKNAME_CHANGE는 닉네임 시스템 구현과 함께 추가할 예정(추가함)
	if (recv_host_header.type != static_cast<int32_t>(PacketType::CHAT_MESSAGE) &&
		recv_host_header.type != static_cast<int32_t>(PacketType::HEADER_ERROR) && 
		recv_host_header.type != static_cast<int32_t>(PacketType::NICKNAME_CHANGE) &&
		recv_host_header.type != static_cast<int32_t>(PacketType::NICKNAME_CHANGE_FAILED) &&
		recv_host_header.type != static_cast<int32_t>(PacketType::NICKNAME_CHANGE_SUCESS)) {
		ClientState.protocol_error = true;
		recv_packet_state.protocol_error = true;
		result.state = recv_packet_state;

		return result;
	}

	/*
	result.type = static_cast<PacketType>(recv_host_header.type);
	result.length = recv_host_header.length; // 이거 기록 안했었음. 그것땜에 항상 result.length == 0으로 판정되는 버그 있었음.
	result.nick = nick_buf;
	*/

	// 페이로드 수신
	packet->payload_up->resize(recv_host_header.length); // 자세한건 브로드캐스트 설계 문서 - RecvPacket() 개편안 참조

	ClientState.payload_recv = true;
	recv_packet_state.payload_recv = true;
	int payload_recv_res = ClientSock->ClientSockRecv(ClientState, (char*) packet->payload_up->data(), recv_host_header.length);

	if (payload_recv_res == SOCKET_ERROR) {
		recv_packet_state.transport_error = true;
		result.state = recv_packet_state;

		return result;
	}
	if (payload_recv_res == 0) {
		recv_packet_state.peer_closed = true;
		result.state = recv_packet_state;

		return result;
	}
	ClientState.payload_recv = false;
	recv_packet_state.payload_recv = false;

	result.packet = std::move(packet);

	/*
	// 문자열로 사용하는지 여부와는 상관없이 해당 바이트열의 끝을 알려주기 위해서 널문자 삽입. 
	buf[recv_host_header.length] = '\0';
	*/

	/*
	if (recv_host_header.type == static_cast<int32_t>(PacketType::HEADER_ERROR)) {
		ClientState.peer_protocol_error = true;
		recv_packet_state.peer_protocol_error = true;
		return recv_packet_state;
	}
	*/

	// 수정 : 무조건 메시지만 수신한게 아니라 패킷을 수신했다는 것을 나타내기 위해서 Message Received : buf가 아닌 Packet Receved.로 로그 메시지 수정
	LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::RECV_COMPLETE, "Packet Received.");

	return result;
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