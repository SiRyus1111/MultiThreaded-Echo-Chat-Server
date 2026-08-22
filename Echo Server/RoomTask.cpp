#include "RoomTask.h"
#include "Room.h"

void JoinRoomTask::Execute(Room& room) {
	room.AddMember(client);
}

void LeaveRoomTask::Execute(Room& room) {
	room.RemoveMember(session_id);
	done.set_value(); // 해당 작업이 실제로 처리되었다는 신호
}

void BroadcastRoomTask::Execute(Room& room) {
	room.RoomBroadcast(packet, sender_id);
}

void ShutdownRoomTask::Execute(Room& room) {
	room.Shutdown();
}
