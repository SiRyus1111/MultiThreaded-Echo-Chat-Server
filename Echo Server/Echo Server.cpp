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
#include <queue>
#include <condition_variable>

#pragma comment(lib, "Ws2_32.lib")

// OOP 싫어... RAII 싫어.. 근데 왜 재밌냐 시발

using SessionID = uint64_t;
using RoomID = uint64_t;
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
class Room;

class Manager : public std::enable_shared_from_this<Manager> {
private:
	std::unordered_map<SessionID, std::shared_ptr<ClientSession>> clients;
	std::mutex clients_mutex;

	std::unordered_map<RoomID, std::shared_ptr<Room>> rooms;
	std::mutex rooms_mutex;
	std::atomic<RoomID> current_room_id_to_be_generated;
public:
	Manager() : current_room_id_to_be_generated(0) {};
	~Manager() {};

	void AddClient(std::shared_ptr<ClientSession> client, SessionID id);

	void RemoveClient(SessionID id);

	void broadcast(std::shared_ptr<Packet> p, SessionID sender_id);

	// ClientSession에서 Manager::clients를 얻을 필요가 있을 때 사용하기 위한 함수
	// SessionID가 이미 ClientSession 객체 내부에 있으므로 std::vector의 형식으로 std::shared_ptr<ClientSession> 객체만 복사함
    std::vector<std::shared_ptr<ClientSession>> GetClients() {

		std::vector<std::shared_ptr<ClientSession>> snapshot;
		snapshot.reserve(clients.size()); // 미리 clients의 크기 이상만큼 메모리를 할당받아서 추가적인 메모리 할당 최적화

		{
			std::lock_guard<std::mutex> lock(clients_mutex);
			
			for (const auto& [id, session_ptr] : clients) {
				snapshot.push_back(session_ptr);
			}
		}

		return snapshot;
	}

	bool CreateRoom(std::shared_ptr<ClientSession> session_to_create);
	bool DeleteRoom(RoomID room_id);

	bool JoinRoom(RoomID room_id, std::shared_ptr<ClientSession> client);
	bool LeaveRoom(std::shared_ptr<ClientSession> client);

	// ClientSession 객체 복사 방지용
	Manager& operator=(const Manager& c) = delete;
	Manager(const Manager&) = delete;
};

class Room : public std::enable_shared_from_this<Room> {
private:
	std::unordered_map<SessionID, std::weak_ptr<ClientSession>> members;
	std::weak_ptr<Manager> manager_wp;
	RoomID room_id;
	std::atomic<bool> shutting = false; // 이거 일반 bool 변수로 바꿔놔야함. 하지만 룸 종료 권한 관련 문제가 생길 경우 CAS 연산이 필요할 수 있기에 현행유지.
	std::mutex room_state_mutex; // 캡슐화(함부로 이 락 잡아서 발생하는 데드락 방지)
public:
	Room(RoomID this_room_id,
		std::shared_ptr<Manager> manager_sp)
		: room_id(this_room_id), manager_wp(manager_sp) {

	}

	std::vector<std::weak_ptr<ClientSession>> GetMembers();

	bool SetInitMember(std::shared_ptr<ClientSession> session_to_create);

	bool AddMember(std::shared_ptr<ClientSession> client);
	bool RemoveMember(SessionID id);

	void Shutdown();
	bool BroadcastThisRoom(std::shared_ptr<Packet>);

};

class ClientSession : public std::enable_shared_from_this<ClientSession> {
private:
	std::unique_ptr<ClientSocket> ClientSock;
	sockaddr_in ClientAddr;
	char ClientAddrStr[INET_ADDRSTRLEN];
	std::weak_ptr<Manager> Manager_wp;
	NetState ClientState; // 단순 값 복사
	std::atomic<bool> closing = false; // alignas(64) 가급적 필요할 듯. false sharing 고려해야함.
	SessionID session_id;
	Nickname nickname;

	std::queue<std::shared_ptr<Packet>> send_queue;
	std::mutex send_queue_mutex;
	std::condition_variable send_queue_cv;

	std::weak_ptr<Room> current_room; // 룸이 없는 상태 : nullptr로 처리
	mutable std::mutex current_room_mutex; // current_room의 읽기 / 쓰기에 대한 mutex, 이 뮤텍스는 const함수(읽기 전용) 함수(GetRoom())에서도 잡으므로 mutable

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
		return static_cast<bool>(closing.load());
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

	std::shared_ptr<Room> GetRoom() const {
		std::lock_guard<std::mutex> lock(current_room_mutex);
		return current_room.lock();
	}

	void SetRoom(std::shared_ptr<Room> room_to_set) {
		std::lock_guard<std::mutex> lock(current_room_mutex);
		current_room = room_to_set;

		return;
	}


	bool SendQueuePush(std::shared_ptr<Packet> packet) {
		{
			std::lock_guard<std::mutex> lock(send_queue_mutex);

			if (closing.load()) {
				return false;
			}

			send_queue.push(std::move(packet));

		}

		// 여기서 Send Queue cv notify_one()
		SendQueueCV_NotifyOne();

		return true;
	}

	std::shared_ptr<Packet> SendQueuePop() {
		std::lock_guard<std::mutex> lock(send_queue_mutex);

		std::shared_ptr<Packet> packet = std::move(send_queue.front());
		send_queue.pop();

		return packet;
	}

	void SendQueueCV_NotifyOne() {
		send_queue_cv.notify_one();
	}

	void SendQueueCV_NotifyAll() {
		send_queue_cv.notify_all();
	}

	void HandleChatMessagePacket(std::shared_ptr<Packet> packet) {
		if (auto manager_sp = Manager_wp.lock()) {
			manager_sp->broadcast(packet, session_id);
		}
	}

	void HandleHeaderErrorPacket(std::shared_ptr<Packet> packet) {
		// 수신한 패킷의 타입이 HEADER_ERROR일 때 실행할 코드
		LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::RECEIVE_ERROR_PACKET, "Received an error packet from a Client.");
		// 여기 수정 필요함. 꼭 기억해두셈. 여기 RemoveThisClient() 함수 없음. CAS 기반 MarkClosing() 함수 추가할 때 이거 추가하셈.
		// 수정 완료
		TryMarkClosing();
	}

	void HandleNicknameChangePacket(std::shared_ptr<Packet> packet) {

		if (ntohl(packet->header.length) > MAX_NICKNAME_LENGTH) {
			// 닉네임 설정 실패 시 정책에 맞게 실패한 이유를 페이로드에 실어서 보냄
			packet->header.type = htonl(static_cast<int32_t>(PacketType::NICKNAME_CHANGE_FAILED));
			memset(&packet->header.nickname, '\0', HEADER_NICKNAME_SIZE); // 패딩 채우기
			memcpy(&packet->header.nickname, SERVER_NICK.c_str(), SERVER_NICK.size());
			*packet->payload_up = nick_length_exceed;
			packet->header.length = htonl(nick_length_exceed.size());

			SendQueuePush(packet);

			return;
			// break하는 코드 추가
		}

		bool nick_already_used = false;
		std::vector<std::shared_ptr<ClientSession>> snapshot;

		if (auto locked = Manager_wp.lock()) {
			snapshot = locked->GetClients();
		}

		for (auto& client : snapshot) {
			if (*packet->payload_up == client->nickname) { // 이거 버그났었음. 
				nick_already_used = true;
				return;
			}
		}

		if (nick_already_used) {
			// 닉네임 설정 실패 시 정책에 맞게 실패한 이유를 페이로드에 실어서 보냄
			packet->header.type = htonl(static_cast<int32_t>(PacketType::NICKNAME_CHANGE_FAILED));
			memset(&packet->header.nickname, '\0', HEADER_NICKNAME_SIZE); // 패딩 채우기
			memcpy(&packet->header.nickname, SERVER_NICK.c_str(), SERVER_NICK.size());
			*packet->payload_up = nick_already_used_msg;
			packet->header.length = htonl(nick_already_used_msg.size());

			SendQueuePush(packet);

			return;
		}
		// 이 이후로는 유효한 닉네임인 경우에만 실행될 수 있음

		nickname = *packet->payload_up;

		// 닉네임 설정 성공 시 클라이언트의 지역 닉네임을 갱신하기 위해 클라이언트가 갱신할 닉네임을 페이로드에 실어서 보내고,
		// 닉네임 설정 성공 메시지는 전적으로 클라이언트에게 책임을 맏김
		packet->header.type = htonl(static_cast<int32_t>(PacketType::NICKNAME_CHANGE_SUCESS));
		memset(&packet->header.nickname, '\0', HEADER_NICKNAME_SIZE); // 패딩 채우기
		memcpy(&packet->header.nickname, SERVER_NICK.c_str(), SERVER_NICK.size());
		// 페이로드는 변경할 닉네임. 그래서 그대로여도 됨.

		SendQueuePush(packet);
	}

	void HandleRecvPacket(std::shared_ptr<Packet> packet) { // 이거 RecvResult가 아닌 Packet 기반으로 수정해야함
		bool quit = false;

		switch (static_cast<PacketType>(ntohl(packet->header.type))) {
	    	case PacketType::CHAT_MESSAGE: // 여기는 SendPacket() 함수 그대로 쓰는걸로 일단..
    		{
				/*
				// 패킷이 공개된 후에는 수정할 수 없다는 불변식 위반 아님. 애초에 패킷이 공개가 안되고 해당 ClientSession 내부에서 처리됨.
				memset(&packet->header.nickname, '\0', HEADER_NICKNAME_SIZE); // 패딩 채우기
				memcpy(&packet->header.nickname, ECHO_NICK.c_str(), ECHO_NICK.size()); // 메모리 카피로 문자열 바이트 그대로 헤더의 닉네임 필드에 넣어버리기


				SendQueuePush(packet);
				*/
				HandleChatMessagePacket(packet);

			    break;
		    }
			case PacketType::HEADER_ERROR:
			{
				HandleHeaderErrorPacket(packet);

				break;
			}
			case PacketType::NICKNAME_CHANGE:
			{
				
				HandleNicknameChangePacket(packet);

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

		// 이 이후를 실행할 수 있는 스레드는 후처리 책임을 가진다.

		if (State.transport_error) {

			if (State.header_recv) LineLogger::GetInstance().WriteSessionLog(session_id, nickname,ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::TRANSPORT_ERROR, "Transport Error occured during Header Receiving.");

			else if (State.payload_recv) LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::TRANSPORT_ERROR, "Transport Error occured during Payload Receiving.");

			else if (State.header_send) LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::TRANSPORT_ERROR, "Transport Error occured during Header Sending.");

			else if (State.payload_send) LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::TRANSPORT_ERROR, "Transport Error occured during Payload Sending.");

		}
		else if (State.protocol_error) {

			LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::PROTOCOL_ERROR, "Protocol Error occured.");

			std::shared_ptr<Packet> packet = std::make_shared<Packet>();

			packet->header.type = htonl(static_cast<int32_t>(PacketType::HEADER_ERROR));
			memset(&packet->header.nickname, '\0', HEADER_NICKNAME_SIZE); // 패딩 채우기
			memcpy(&packet->header.nickname, SERVER_NICK.c_str(), SERVER_NICK.size());
			*packet->payload_up = header_err_msg;
			packet->header.length = htonl(strlen(header_err_msg));

			LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::SEND_ERROR_PACKET, "Sending an error packet...");

			SendQueuePush(packet);

		}
		else if (State.peer_closed) {
			LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::DISCONNECTED, "The client has successfully closed the connection.");
		}

		return;
	}

	// share_from_this()로 받기 / Manager 객체에서 사용하는 함수
	void AddToManager(std::shared_ptr<Manager> Manager_sp) {
		Manager_wp = Manager_sp;
		return;
	}

	// 송수신 로직이 구현되어있는 함수
	void Run();

	void RecvRun();

	void SendRun();

	NetState SendPacket(std::shared_ptr<Packet> packet);

	RecvResult RecvPacket();

	bool TryMarkClosing() {
		bool expected = false;
		{
			std::lock_guard<std::mutex> lock(send_queue_mutex); // send thread의 lost wakeup을 막기 위해 send thread의 조건 변수의 조건인 closing의 변경에 락을 걸음.
			if (!closing.compare_exchange_strong(expected, true)) {
				return false;
			}
		}
			

		// 이 이후를 실행할 수 있는 스레드는 종료 책임을 가진다.

		if (!current_room.expired()) { // 어떤 룸에 소속되어있는 경우에 대한 처리
			if (auto manager_sp = Manager_wp.lock()) {
				manager_sp->LeaveRoom(shared_from_this()); // LeaveRoom() 함수 도중에 룸이 폭파되어서 false가 반환된 경우도 상관 없음(애초에 이 코드는 룸이 폭파된 경우(current_room == nullptr) 실행되지 않아도 됨)
			}
		}

		ClientSock->ClientSockShutdown(); // 만약 SOCKET_ERROR를 반환해도 상관없음. 그러면 Recv / Send도 안되는거 아님?
		RemoveThisClient();

		return true;
	}

	void RemoveThisClient() {
		if (auto locked = Manager_wp.lock()) {
			locked->RemoveClient(session_id);
		}
		else {
			std::cout << "Manager 객체 이미 소멸됨. RemoveClient()가 실행되지 않습니다.\n";
		}
	}

	// ClientSession 객체 복사 방지용
	ClientSession& operator=(const ClientSession& c) = delete;
	ClientSession(const ClientSession&) = delete;

};

// 룸을 생성하는 함수
bool Manager::CreateRoom(std::shared_ptr<ClientSession> session_to_create) {
	if (!session_to_create) return false; // 먼저 유효한 세션인지 확인

	uint64_t this_room_id = current_room_id_to_be_generated.fetch_add(1); // current_room_id_to_be_generated를 원자적으로 1 증가시키며 해당 룸 아이디 생성(fetch_add가 여러 스레드일 경우 문제생길 수 있어서 추후 수정 예정)

	auto new_room = std::make_shared<Room>(this_room_id, shared_from_this()); // Room 객체 생성

	if (!new_room->SetInitMember(session_to_create)) { // 해당 룸을 생성한 세션이 자동으로 해당 룸에 들어가도록 설정, 만약 실패할 경우 생성된 룸을 rooms에 넣지 않음, 그렇게 된다면 해당 Room 객체는 함수 종료 시 자동으로 소멸됨
		return false;
	}

	std::lock_guard<std::mutex> lock(rooms_mutex);

	rooms[this_room_id] = std::move(new_room); // 생성된 룸을 rooms에 넣음

	return true;
}

bool Manager::DeleteRoom(RoomID room_id) {
	std::shared_ptr<Room> target;
	{
		std::lock_guard<std::mutex> lock(rooms_mutex);

		auto it = rooms.find(room_id); // 이미 다른 스레드가 삭제한 경우를 대비해서 [] 기반 접근이 아닌 find()로 해결함
		if (it == rooms.end()) return false;

		target = it->second;

		rooms.erase(it);
	}
	target->Shutdown(); // 누가 Room을 변경(AddMember() 등)하고 있을 경우 해당 함수가 가지고있는 room_state_mutex를 못잡아서 blocking됨. 즉, target이 소멸되지 않음.

	return true;
}

bool Manager::JoinRoom(RoomID room_id, std::shared_ptr<ClientSession> client) {

	// 이미 다른 룸에 소속되어있는 경우는 AddMember() 함수가 검사함.

	if (client == nullptr) {
		false;
	}

	// AddMember() 호출 및 추가 코드 + 락(rooms_mutex) 잡음
	std::shared_ptr<Room> target;
	{
		std::lock_guard<std::mutex> lock(rooms_mutex);

		auto it = rooms.find(room_id);
		if (it == rooms.end()) return false;

		target = it->second;
	}

	return target->AddMember(client);
}

// JoinRoom()과 추상화 수준을 맞추기 위해서 rooms 목록을 사용할 필요가 없지만 Manager를 통해서 LeaveRoom()을 수행한다.
bool Manager::LeaveRoom(std::shared_ptr<ClientSession> client) {
	// RemoveMember() 호출 및 추가 코드 + 락 잡음
	if (client == nullptr) { // 혹시 모를 예외처리
		return false;
	}

	// Manager::rooms를 건들지 않으니 rooms_mutex를 잡을 이유 없음

	auto room_to_leave = client->GetRoom();
	if (room_to_leave == nullptr) { // 만약 이미 룸이 터진 경우(LeaveRoom() 함수 호출과 GetRoom() 함수 호출 사이에 룸이 터지면 이게 가능함)
		return false; // 어차피 룸이 터지면 ClientSession::current_room도 무효화되니까 이것만 해도 상관없음.
	}

	return room_to_leave->RemoveMember(client->GetSessionID());
}

// 소멸자에서 Room 소멸 Logging

// 룸 초기화 전용 함수
// 호출 시 해당 객체에 대한 shared_ptr로 호출함
bool Room::SetInitMember(std::shared_ptr<ClientSession> session_to_create) {
	if (!session_to_create) return false;

	SessionID id = session_to_create->GetSessionID();

	std::lock_guard<std::mutex> lock(room_state_mutex);

	if (shutting.load()) return false;

	// 불변식 3, 4 만족(members에 추가 / current_room 참조가 둘 다 성공하거나 둘 다 실패함.) - 근데 이거 도중에 해당 룸이 날아가면 어떻게되냐? - 문제 없음. 5-1 참조
	session_to_create->SetRoom(shared_from_this()); // 이미 shared_ptr로 생성된 객체이므로 문제 없음!
	members[id] = session_to_create;
	return true;
}

// 초기화 후 다른 클라이언트가 해당 룸에 들어올 때 전용 함수
// 호출 시 해당 객체에 대한 shared_ptr로 호출함
// 호출 주체 : Manager
bool Room::AddMember(std::shared_ptr<ClientSession> client) {
	if ((client == nullptr) || (client->GetRoom().get() != this)) { // client가 유효하지 않거나 이미 다른 룸에 소속되어있는 경우
		return false;
	}

	SessionID id = client->GetSessionID();

	std::lock_guard<std::mutex> lock(room_state_mutex); // shutting을 변경하려면 해당 락을 잡아야됨. 그래서.. (5-3 참조)

	if (shutting.load()) return false;

	// 여기도 불변식 3, 4 만족 - 같은 문제 공유
	client->SetRoom(shared_from_this());
	members[id] = client;
	return true;
}

// session_id를 받아 해당 session id의 클라이언트를 룸에서 제거하는 함수
// 호출 시 해당 객체에 대한 shared_ptr로 호출함
// 호출 주체 : Manager
bool Room::RemoveMember(SessionID id) {
	std::lock_guard<std::mutex> lock(room_state_mutex);

	if (shutting.load()) return false;

	auto it = members.find(id);
	if (it == members.end()) return false;

	if (auto client_sp = it->second.lock()) {
		if (client_sp->GetRoom().get() == this) { // 아직도 나를 가리킬 때만 ClientSession::current_room을 nullptr로
			client_sp->SetRoom(nullptr);
		}
	}
	// 해당 client 객체가 사라졌어도 별다른 처리를 하지 않는다.

	members.erase(it); // 이미 해당 객체가 사라졌어도 weak_ptr 지우기

	return true;
}

std::vector<std::weak_ptr<ClientSession>> Room::GetMembers() {
	std::vector<std::weak_ptr<ClientSession>> snapshot;

	{
		std::lock_guard<std::mutex> lock(room_state_mutex);

		// 락 잡은 상태에서 shutting 플래그를 확인해서 Shutdown() 함수가 호출됐을 때 shutting == true를 놓칠 가능성 없앰
		// 다른 shutting 플래그 확인하는 부분에서도 이 논리로 락을 잡은 상태로 shutting 플래그를 확인함
		if (shutting.load()) { // 만약 룸이 논리적 폐쇄됐다면 빈 스냅샷 반환
			return snapshot;
		}

		snapshot.reserve(members.size()); // 미리 members의 크기 이상만큼 메모리를 할당받아서 추가적인 메모리 할당 최적화

		for (const auto& [id, session_ptr] : members) {
			snapshot.push_back(session_ptr);
		}
	}

	return snapshot;
}

// 해당 함수가 락을 잡아야 shutting이 true가 됨
void Room::Shutdown() {

	bool expected = false;

	std::lock_guard<std::mutex> lock(room_state_mutex);


	if (!shutting.compare_exchange_strong(expected, true)) { // 이미 다른 스레드가 shutting == true로 바꿨다면 해당 스레드는 shutdown 절차를 진행하지 않음
		return;
	}

	// CAS 연산으로, shutdown 절차를 진행할 책임을 처음으로 shutting = true로 바꾼 스레드에게 넘김.

	for (auto& [id, client_wp] : members) {
		if (auto client_sp = client_wp.lock()) {
			client_sp->SetRoom(nullptr);
		}
	}

	members.clear();
}

bool Room::BroadcastThisRoom(std::shared_ptr<Packet>) {
	std::vector broadcast_members = GetMembers();

	// 여기 미완

	return true;
}

void Manager::AddClient(std::shared_ptr<ClientSession> client, SessionID id) {

	// ClientSession 객체에 shared_ptr을 넘겨줘서 ClientSession 객체의 weak_ptr을 초기화.
	client->AddToManager(shared_from_this());

	std::lock_guard<std::mutex> lock(clients_mutex); // 락을 최대한 짧게 잡고있기. DeadLock 위험 감소, Lock Contention 감소.
	clients[id] = client;

	return;
};

void Manager::RemoveClient(const SessionID id) {
	// 여기에 clients에서 해당 SessionID의 ClientSession만 지우는 로직의 코드
	std::lock_guard<std::mutex> lock(clients_mutex);
	clients.erase(id);
}

void Manager::broadcast(std::shared_ptr<Packet> p, const SessionID sender_id) {
	auto snapshot = GetClients();

	for (auto& client_info : snapshot) {
		if (client_info->GetClosing()) {
			continue;
		}
		if (client_info->GetSessionID() == sender_id) {
			continue;
		}

		client_info->SendQueuePush(p);
	}
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

void ClientSession::RecvRun() {
	while (true) {
		RecvResult recv_res = RecvPacket();

		if (closing.load()) { // send thread나 다른 스레드에서 바꾸고 shutdown() 호출했을 때 확인용
			return;
		}

		if (recv_res.state.transport_error ||
			recv_res.state.protocol_error ||
			recv_res.state.peer_closed) {
			HandleTransportException(recv_res.state); // 여기서 closing == true로 바꿈
			SendQueueCV_NotifyAll(); // 여기서 notify_all 호출하니까 send thread가 closing == true를 인식 못하는 문제는 없음

			break;
		}

		HandleRecvPacket(std::move(recv_res.packet));

		if (closing.load()) { // HandleRecvPacket() 에서 바꾼거 확인용
			return;
		}
	}
}

void ClientSession::SendRun() {
	while (true) {
		{
			std::unique_lock<std::mutex> lock(send_queue_mutex); // 디버깅 포인트 : 여기 락이랑 밑의 SendQueuePop() 에서 동시에 같은 락을 잡으려 했었음.
			while (!(closing.load() || !send_queue.empty())) { // 드 모르간 적용, (!closing && empty()), 종료 상태가 아니고 큐가 비어있다면 다음으로 진행하지 않는다.
				send_queue_cv.wait(lock);
			}
		}

		if (closing.load()) { // recv thread나 다른 스레드에서 바꾸고 깨웠을 때 확인용
			return;
		}

		std::shared_ptr<Packet> packet = SendQueuePop();

		NetState send_res = SendPacket(packet);

		if (send_res.transport_error ||
			send_res.protocol_error ||
			send_res.peer_closed) {
			HandleTransportException(send_res);
			break;
		}
	}

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

    // Packet 구조체 기반 송신에는 닉네임 길이 검사를 할 필요가 있나 싶음..

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

	// 수정 : 무조건 메시지만 수신한게 아니라 패킷을 수신했다는 것을 나타내기 위해서 Message Received : buf가 아닌 Packet Receved.로 로그 메시지 수정
	LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::RECV_COMPLETE, "Packet Received.");

	return result;
}



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