#include "ClientSession.h"
#include "Manager.h"

std::unique_lock<std::mutex> ClientSession::GetCurrentRoomLock() {
	std::unique_lock<std::mutex> lock(current_room_mutex, std::defer_lock); // 잠그지 않고 전달하기 위해 std::defer_lock 옵션 사용
	return lock;
}

void ClientSession::HandleChatMessagePacket(std::shared_ptr<Packet> packet) {
	if (auto manager_sp = Manager_wp.lock()) {
		manager_sp->broadcast(packet, session_id);
	}
}

void ClientSession::HandleNicknameChangePacket(std::shared_ptr<Packet> packet) {
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
			break;
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

	return;
}

bool ClientSession::TryMarkClosing() {
	bool expected = false;
	{
		std::lock_guard<std::mutex> lock(send_queue_mutex); // send thread의 lost wakeup을 막기 위해 send thread의 조건 변수의 조건인 closing의 변경에 락을 걸음.
		if (!closing.compare_exchange_strong(expected, true)) {
			return false;
		}
	}


	// 이 이후를 실행할 수 있는 스레드는 종료 책임을 가진다.

    // 이 사이에 AddMember()가 실행되어도 어차피 룸을 얻는 것은 current_room_mutex때문에 해당 룸이 변경된 후이기 때문에 문제 없음
    // 해당 룸에 LeaveRoom()을 호출하게 됨

    std::shared_ptr<Room> room;
    {
        std::lock_guard<std::mutex> lock(current_room_mutex);
        room = current_room.lock(); // closing=true 이후(이거 중요함), 락 안에서 '지금' 값 다시 읽기

        // Closing 처리와 이 과정의 실행 사이에 AddMember()가 수행되어도
        // AddMember()가 락을 먼저 잡은 경우 : 룸에 입장 후 뒤의 LeaveRoom()이 실행됨
        // 이 함수가 먼저 락을 잡은 경우 : 이미 closing == true이므로 AddMember()가 실행되지 않음.
    }

    if (room != nullptr) {
        if (auto mamager_sp = Manager_wp.lock()) {

            if (!mamager_sp->LeaveRoom(shared_from_this())) { // 만약 LeaveRoom() 함수가 실패한 경우(이미 해당 룸이 폭파될 예정)
                current_room.reset(); // 자신의 current_room만 빠르게 reset()
            }
        }
    }

	ClientSock->ClientSockShutdown(); // 만약 SOCKET_ERROR를 반환해도 상관없음. 그러면 Recv / Send도 안되는거 아님?
	RemoveThisClient();

	return true;
}

void ClientSession::RemoveThisClient() {
	if (auto locked = Manager_wp.lock()) {
		locked->RemoveClient(session_id);
	}
	else {
		std::cout << "Manager 객체 이미 소멸됨. RemoveClient()가 실행되지 않습니다.\n";
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


    // 패킷 유효성 검사
	if (!VerifySendPacket(packet)){
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

	if (send_host_header.length == 0) { // 페이로드 송신 전 빈 페이로드 검사(예외 아님)
		// 굳이 페이로드를 전송 할 필요 없음(페이로드가 0바이트)
		return send_packet_state;
	}

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

	if (!VerifyRecvPacket(packet)) {
		ClientState.protocol_error = true;
		recv_packet_state.protocol_error = true;
		result.state = recv_packet_state;

		return result;
	}

	// 유효성 검사 후에 페이로드 0 검사를 해야 유효하지 않은 패킷을 수신하는 문제 예방 가능
	if (recv_host_header.length == 0) {
		result.packet->payload_up->clear(); // 받을 페이로드가 없으므로 빈 문자열로 세팅
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
