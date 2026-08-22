#pragma once

#include <unordered_map>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <vector>
#include <memory>
#include "Types.h"
#include "RoomTask.h"
#include "LineLogger.h"

class ClientSession;
class Manager;

class Room : public std::enable_shared_from_this<Room> {
private:
	const RoomID room_id; // RoomID는 바뀔 일이 없으므로
	std::unordered_map<SessionID, std::weak_ptr<ClientSession>> members;
	std::weak_ptr<Manager> manager_wp;
	std::mutex tasks_mutex; // tasks 메시지 큐와 CV용 shutting 플래그에 대한 뮤텍스
	std::queue<std::shared_ptr<RoomTask>> tasks; // 해당 룸에 상태 변경에 대한 Command들을 담는 메시지 큐
	bool shutting; // 해당 룸 자체의 논리적 종료 상태
	bool is_tasks_shutting; // ShutdownRoomTask push() 시 더이상 tasks 큐에 아무것도 못 들어오게 하기 위한 용도
	std::condition_variable tasks_and_shutting_cv;
public:
	Room(RoomID this_room_id,
		std::shared_ptr<Manager> manager_sp)
		: room_id(this_room_id), manager_wp(manager_sp), shutting(false), is_tasks_shutting(false) {
		LineLogger::GetInstance().WriteLog("[Room Create] The room has been created. Room ID : ", room_id);
	}
	~Room() {
		LineLogger::GetInstance().WriteLog("[Room Deleted] The room has been closed. Room ID : ", room_id);
	}

	bool RoomTasksPush(std::shared_ptr<RoomTask> task_requested_by_client);
	std::shared_ptr<RoomTask> RoomTasksPop();

	bool AddMember(std::shared_ptr<ClientSession> client);
	bool RemoveMember(SessionID session_id);

	std::vector<std::weak_ptr<ClientSession>> GetMembers() const;
	RoomID GetRoomID() const;

	void RoomBroadcast(std::shared_ptr<Packet> packet, SessionID sender_id);

	void Shutdown();

	void RoomRun();
};
