#include "RoomTask.h"
#include "Room.h"

void JoinRoomTask::Execute(Room& room) {
	res.set_value(room.AddMember(client));
}

void LeaveRoomTask::Execute(Room& room) {
	res.set_value(room.RemoveMember(session_id)); // 해당 작업이 실제로 처리되었다는 신호
}

void BroadcastRoomTask::Execute(Room& room) {
	room.RoomBroadcast(packet, sender_id);
}

void ShutdownRoomTask::Execute(Room& room) {
	room.Shutdown();
}
