#pragma once

#include <memory>
#include <future>
#include "Types.h"

class Room;
class ClientSession;

class RoomTask {
public:
	virtual ~RoomTask() = default;
	virtual void Execute(Room& room) = 0;
};

class JoinRoomTask : public RoomTask {
private:
	std::shared_ptr<ClientSession> client;
    std::promise<bool> res;
public:
	JoinRoomTask(std::shared_ptr<ClientSession> _client) : client(_client) {

	}
	std::future<bool> GetFuture() {
		return res.get_future();
	}
	void Execute(Room& room) override;
};

class LeaveRoomTask : public RoomTask {
private:
	SessionID session_id;
	std::promise<bool> res;
public:
	LeaveRoomTask(SessionID _session_id) : session_id(_session_id) {

	}
	std::future<bool> GetFuture() { // 작업이 끝났음을 나타내는 future를 get하는 getter 함수
		return res.get_future();
	}
	void Execute(Room& room) override;
};

class BroadcastRoomTask : public RoomTask {
private:
	std::shared_ptr<Packet> packet;
	SessionID sender_id;
public:
	BroadcastRoomTask(std::shared_ptr<Packet> _packet, SessionID _sender_id) : packet(_packet), sender_id(_sender_id) {

	}
	void Execute(Room& room) override;
};

class ShutdownRoomTask : public RoomTask {
public:
	ShutdownRoomTask() {

	}
	void Execute(Room& room);
};
