#include "Manager.h"
#include "ClientSession.h"
#include "Room.h"
#include "RoomTask.h"

void Manager::RemoveRoomToManager(RoomID room_id) {
	{
		std::lock_guard<std::mutex> lock(rooms_mutex);
		rooms.erase(room_id);
	}
}

bool Manager::LeaveRoom(std::shared_ptr<ClientSession> client) {
	auto room = client->GetRoom();
	if (!room) return false; // 이미 룸이 삭제되어서 GetRoom이 nullptr를 반환하는 것에 대한 예외처리

	auto task = std::make_shared<LeaveRoomTask>(client->GetSessionID());
	auto future = task->GetFuture();

	if (!room->RoomTasksPush(task)) return false; // 이미 is_tasks_shutting이면 여기서 false

	future.wait(); // RemoveMember()가 실제로 끝날 때까지 호출 스레드가 대기(RemoveMember() 실행 확인)
	return true;
}

bool Manager::JoinRoom(RoomID room_id, std::shared_ptr<ClientSession> client) {
	std::shared_ptr<Room> room;
	{
		std::lock_guard<std::mutex> lock(rooms_mutex);

		auto it = rooms.find(room_id);
		if (it == rooms.end()) {
			return false;
		}

		room = it->second;
	}

	auto task = std::make_shared<JoinRoomTask>(client);

	if (!room->RoomTasksPush(task)) {
		return false;
	}

	return true;

}

bool Manager::DeleteRoom(RoomID room_id) {
	std::shared_ptr<Room> room;
	{
		std::lock_guard<std::mutex> lock(rooms_mutex);

		auto it = rooms.find(room_id);
		if (it == rooms.end()) {
			return false;
		}

		room = it->second;
	}

	auto task = std::make_shared<ShutdownRoomTask>();

	if (!room->RoomTasksPush(task)) {
		return false;
	}

	// 어차피 join()이 기다리는 함수니 Shutdown()이 종료될 때까지 기다릴 필요는 없음.
	// Room::shutdown()이 실행됐다면 shutting == true일 것이므로 해당 스레드는 종료됨
	{
		std::lock_guard<std::mutex> lock(rooms_threads_mutex);

		auto it = rooms_threads.find(room_id);
		if (it != rooms_threads.end()) {
			std::thread this_room_thread = std::move(it->second);

			if (this_room_thread.joinable()) {
				this_room_thread.join();
			}
		}
	}


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
