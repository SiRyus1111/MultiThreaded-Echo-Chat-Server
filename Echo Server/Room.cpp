#include "Room.h"
#include "ClientSession.h"
#include "Manager.h"

bool Room::RoomTasksPush(std::shared_ptr<RoomTask> task_requested_by_client) {
	{
		std::lock_guard<std::mutex> lock(tasks_mutex);

		if (is_tasks_shutting) {
			return false;
		}
		if (dynamic_cast<ShutdownRoomTask*>(task_requested_by_client.get())) { // 다운캐스팅해서 입력받은 RoomTask가 ShutdownRoomTask인지 판별
			is_tasks_shutting = true;
		}

		tasks.push(task_requested_by_client);

	}

	tasks_and_shutting_cv.notify_one(); // 하나의 RoomTask가 push되었으니 잠들어있는 해당 룸의 스레드를 깨움

	return true;
}

std::shared_ptr<RoomTask> Room::RoomTasksPop() {
	std::shared_ptr<RoomTask> result;
	{
		std::lock_guard<std::mutex> lock(tasks_mutex);
		result = tasks.front(); // tasks 큐의 맨 앞 작업 꺼내서 shared_ptr 복사
		tasks.pop();
	}

	return result;
}

bool Room::AddMember(std::shared_ptr<ClientSession> client) {
	if (!client || (client->GetRoom() != nullptr)) false;

	// 정책을 ShutdownRoomTask가 push() 된 후에는 새로운 작업을 큐에 넣지 않는 것으로 잡았으므로 shutting == true 검사는 필요 없음

	// 룸을 수정하는 주체가 스레드 하나이기에 룸에 상태에 대한 락은 필요 없음,
	// 하지만 클라이언트의 current_room을 수정하는 스레드는 여러개일 수 있기 때문에 current_room_mutex의 unique_lock을 얻음
	std::unique_lock<std::mutex> client_lock = client->GetCurrentRoomLock();
	client_lock.lock();

	// members가 변하면 -> current_room도 변하도록 미리 current_room_mutex를 잡아둠
	// 중간에 다른 스레드가 current_room을 변경할 수 없어 room.members contains client <-> client.current_room == room이 성립함
    client->SetRoomUnlocked(shared_from_this());
	members[client->GetSessionID()] = client;

	return true;
}

bool Room::RemoveMember(SessionID session_id) {

	auto it = members.find(session_id);
	if (it == members.end()) return false;

	auto client_wp = it->second;

	if (auto client_sp = client_wp.lock()) {
		std::unique_lock<std::mutex> client_lock = client_sp->GetCurrentRoomLock();
		client_lock.lock(); // 위와 같은 이유로 current_room_mutex를 먼저 잡고 current_room / members에 대한 변경을 진행함

		client_sp->SetRoom(nullptr);
		members.erase(session_id);
	}


	return true;
}

std::vector<std::weak_ptr<ClientSession>> Room::GetMembers() const {
	std::vector<std::weak_ptr<ClientSession>> snapshot;
	snapshot.reserve(members.size());

	for (auto [id, client_wp] : members) {
		snapshot.push_back(client_wp);
	}

	return snapshot;
}

RoomID Room::GetRoomID() const {
	return room_id;
}

void Room::RoomBroadcast(std::shared_ptr<Packet> packet, SessionID sender_id) {
	for (auto [id, client_wp] : members) {
		if (auto client_sp = client_wp.lock()) {
			if (client_sp->GetSessionID() == sender_id) {
				continue;
			}
			client_sp->SendQueuePush(packet);
		}

	}
}

void Room::Shutdown() {
	shutting = true;

	// 룸 삭제 패킷 세팅
    std::shared_ptr<Packet> packet;
    packet->header.type = static_cast<int32_t>(PacketType::ROOM_DELETED);
    packet->header.length = 0;

	memset(packet->header.nickname, '\0', HEADER_NICKNAME_SIZE);
    memcpy(packet->header.nickname, SERVER_NICK.c_str(), SERVER_NICK.size());
    
    // 페이로드 세팅은 하지 않음(빈 페이로드로 보냄)
    packet->payload_up->clear();


	for (auto [id, client_wp] : members) {
		if (auto client_sp = client_wp.lock()) {
			client_sp->SendQueuePush(packet);
			client_sp->SetRoom(nullptr);
		}
	}

	members.clear();

	if (auto manager_sp = manager_wp.lock()) {
		manager_sp->RemoveRoomToManager(room_id);
	}
}

void Room::RoomRun() {
	while (true) {
		{
			std::unique_lock<std::mutex> lock(tasks_mutex);
			while (tasks.empty() && !shutting) { // 아직 큐가 비어있고 shutting == false라면 깨어나면 안됨
				tasks_and_shutting_cv.wait(lock);
			}

			// 만약 방금 실행(Execute())한 작업이 ShutdownRoomTask여도 while문 검사에서 !shutting으로 wait() 함수를 호출하지 않게 되고,
			// 해당 if문을 만나서 안전하게 break됨.
			// 더 이상 작업이 없게 되어서 영원히 대기하지 않음.
			if (shutting) {
				break;
			}
		}

		auto task = RoomTasksPop();

		task->Execute(*this);

	}
}
