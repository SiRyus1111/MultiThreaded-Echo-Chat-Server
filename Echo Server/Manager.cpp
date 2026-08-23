#include "Manager.h"
#include "ClientSession.h"
#include "Room.h"
#include "RoomTask.h"

void room_thread(std::shared_ptr<Room> room) {
	room->RoomRun();
}

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

	if (!future.get()) { // RemoveMember()가 실제로 끝날 때까지 호출 스레드가 대기(RemoveMember() 실행 확인)(성공 / 실패 여부까지 받아서 불변식 깨지 않나 확인)
	    return false;
	}
	
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
	auto future = task->GetFuture();

	if (!room->RoomTasksPush(task)) {
		return false;
	}

	if (!future.get()) {
        return false;
    }

	return true;

}

bool Manager::CreateRoom(std::shared_ptr<ClientSession> client_to_create) {
    if (client_to_create == nullptr) {
        return false;
    }

    RoomID this_room_id = current_room_id_to_be_generated.fetch_add(1);
    std::shared_ptr<Room> this_room = std::make_shared<Room>(this_room_id, shared_from_this());

    std::thread assigned_thread(room_thread, this_room);

    auto task = std::make_shared<JoinRoomTask>(client_to_create);
    auto future = task->GetFuture();

    this_room->RoomTasksPush(task);

    if (!future.get()) {
        return false;
    }

    // 앞 구간에서 예외가 터지면 정상적인 룸이 아닐 것이기 떄문에 룸이 그대로 증발하도록 룸 목록에 추가를 마지막에 함
    {
        std::lock_guard<std::mutex> rooms_lock(rooms_mutex);
        rooms[this_room_id] = this_room;
    }

    {
        std::lock_guard<std::mutex> rooms_threads_lock(rooms_threads_mutex);
        rooms_threads[this_room_id] = std::move(assigned_thread);
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
    std::thread this_room_thread;
    {
        std::lock_guard<std::mutex> lock(rooms_threads_mutex);

        auto it = rooms_threads.find(room_id);
        if (it == rooms_threads.end()) {
            return false;
        }
		
		this_room_thread = std::move(it->second);
		rooms_threads.erase(room_id);
    }

    // 오래 걸릴지도 모르는 무거운 join()은 lock을 잡지 않고 실행
    if (this_room_thread.joinable()) {
        this_room_thread.join();
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
