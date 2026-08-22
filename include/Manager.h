#pragma once

#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include "Types.h"

class ClientSession;
class Room;

class Manager : public std::enable_shared_from_this<Manager> {
private:
	std::unordered_map<SessionID, std::shared_ptr<ClientSession>> clients;
	std::mutex clients_mutex;
	std::unordered_map<RoomID, std::shared_ptr<Room>> rooms;
	std::unordered_map<RoomID, std::thread> rooms_threads; // 룸들의 스레드를 보관해놓은 컨테이너
	std::mutex rooms_mutex; // 읽기 / 쓰기 락, unordered_map은 읽기와 쓰기가 같이 있으면 읽기가
	std::mutex rooms_threads_mutex;
	std::atomic<RoomID> current_room_id_to_be_generated;
public:
	Manager() {};
	~Manager() {};

	void AddClient(std::shared_ptr<ClientSession> client, SessionID id);

	void RemoveClient(SessionID id);

	bool CreateRoom(std::shared_ptr<ClientSession> client_to_create);
	bool DeleteRoom(RoomID room_id);

	bool JoinRoom(RoomID room_id, std::shared_ptr<ClientSession> client);
	bool LeaveRoom(std::shared_ptr<ClientSession> client);

	void RemoveRoomToManager(RoomID room_id);

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

	// ClientSession 객체 복사 방지용
	Manager& operator=(const Manager& c) = delete;
	Manager(const Manager&) = delete;
};
