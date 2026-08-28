#pragma once

#include <memory>
#include <string>
#include <sstream>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <iostream>
#include "Types.h"
#include "socketRAII.h"
#include "LineLogger.h"

class Manager;
class Room;

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

	void SetRoom(std::shared_ptr<Room> room_to_set) {
		std::lock_guard<std::mutex> lock(current_room_mutex);
		SetRoomUnlocked(room_to_set); // 락 잡은 채로 내부 버전 호출
	}
	void SetRoomUnlocked(std::shared_ptr<Room> room_to_set) {
		current_room = room_to_set;
	}

	std::shared_ptr<Room> GetRoom() const {
        std::lock_guard<std::mutex> lock(current_room_mutex);
        return GetRoomUnlocked();
    }
    std::shared_ptr<Room> GetRoomUnlocked() const {
        return current_room.lock();
    }

	std::unique_lock<std::mutex> GetCurrentRoomLock();

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

	// 페이로드 수신 전에 해당 패킷을 넣어버려도 페이로드 사용 안하니까 괜찮음
	bool VerifyRecvPacket(std::shared_ptr<Packet> packet) {
    	PacketType type = static_cast<PacketType>(packet->header.type);
    
	    // 검사해야할 조건이 많고
    	// 다양한 패킷 타입이 있으므로
    	// 코드가 지나치게 복잡해질 수 있어서
    	// 패킷 타입의 카테고리에 따라 다른 bool 변수로 타입을 받음.
    	bool is_init_packet_type = (type == PacketType::CHAT_MESSAGE)
        	                    || (type == PacketType::HEADER_ERROR);
    
    	bool is_nick_packet_type = (type == PacketType::NICKNAME_CHANGE);

	    bool is_room_packet_type = (type == PacketType::JOIN_ROOM) 
    	                        || (type == PacketType::LEAVE_ROOM)
        	                    || (type == PacketType::ROOM_MESSAGE)
            	                || (type == PacketType::CREATE_ROOM)
                	            || (type == PacketType::DELETE_ROOM);

		bool is_length_valid = (packet->header.length <= PAYLOAD_SIZE);

    	return (is_init_packet_type || is_nick_packet_type || is_room_packet_type) && is_length_valid;
	}

	bool VerifySendPacket(std::shared_ptr<Packet> packet) {
    	PacketType type = static_cast<PacketType>(packet->header.type);

	    bool is_init_packet_type = (type == PacketType::CHAT_MESSAGE)
    	                        || (type == PacketType::HEADER_ERROR);

	    bool is_nick_packet_type = (type == PacketType::NICKNAME_CHANGE_SUCESS)
    	                        || (type == PacketType::NICKNAME_CHANGE_FAILED);
                        
    	bool is_room_packet_type = (type == PacketType::ROOM_MESSAGE)
        	                    || (type == PacketType::JOIN_ROOM_SUCCESS)
            	                || (type == PacketType::LEAVE_ROOM_SUCCESS)
                	            || (type == PacketType::CREATE_ROOM_SUCCESS)
                    	        || (type == PacketType::DELETE_ROOM_SUCCESS)
                        	    || (type == PacketType::JOIN_ROOM_FAILED)
      	                        || (type == PacketType::LEAVE_ROOM_FAILED)
        	                    || (type == PacketType::CREATE_ROOM_FAILED)
            	                || (type == PacketType::DELETE_ROOM_FAILED)
                	            || (type == PacketType::ROOM_DELETED);
		
		bool is_length_valid = (packet->header.length <= PAYLOAD_SIZE);

    	return (is_init_packet_type || is_nick_packet_type || is_room_packet_type) && is_length_valid;
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

	void HandleChatMessagePacket(std::shared_ptr<Packet> packet);

	void HandleHeaderErrorPacket(std::shared_ptr<Packet> packet) {
		// 수신한 패킷의 타입이 HEADER_ERROR일 때 실행할 코드
		LineLogger::GetInstance().WriteSessionLog(session_id, nickname, ClientAddrStr, ntohs(ClientAddr.sin_port), LineLogger::LogType::RECEIVE_ERROR_PACKET, "Received an error packet from a Client.");
		// 여기 수정 필요함. 꼭 기억해두셈. 여기 RemoveThisClient() 함수 없음. CAS 기반 MarkClosing() 함수 추가할 때 이거 추가하셈.
		// 수정 완료
		TryMarkClosing();

	}

	void HandleNicknameChangePacket(std::shared_ptr<Packet> packet);

	void HandleRecvPacket(std::shared_ptr<Packet> packet) { // 이거 RecvResult가 아닌 Packet 기반으로 수정해야함
		bool quit = false;

		switch (static_cast<PacketType>(ntohl(packet->header.type))) {
	    	case PacketType::CHAT_MESSAGE:
    		{

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

	bool TryMarkClosing();

	void RemoveThisClient();

	// ClientSession 객체 복사 방지용
	ClientSession& operator=(const ClientSession& c) = delete;
	ClientSession(const ClientSession&) = delete;

};
