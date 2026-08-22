#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <memory>
#include "NetCommon.h"

using SessionID = uint64_t;
using RoomID = uint64_t;
using Nickname = std::string;

const int SERVER_PORT = 9000;
const int PAYLOAD_SIZE = 4096;
const int BUFFER_SIZE = PAYLOAD_SIZE + 1; // \0을 맨 끝에 추가해야하므로
const int HEADER_TYPE_SIZE = 4;
const int HEADER_LENGTH_SIZE = 4;
const int HEADER_NICKNAME_SIZE = 32; // 헤더의 닉네임 필드 크기
const int HEADER_SIZE = HEADER_TYPE_SIZE + HEADER_LENGTH_SIZE + HEADER_NICKNAME_SIZE;

const SessionID INITIAL_SESSION_ID = 0;
const size_t MAX_NICKNAME_LENGTH = 32; // 가능한 닉네임 최대 길이(HEADER_NICKNAME_SIZE와 의미가 다름)
const Nickname ECHO_NICK = "EchoFromServer";
const Nickname SERVER_NICK = "ServerMessage";

const char header_err_msg[] = "The header is invalid. The server is terminating the connection.";
inline uint32_t host_err_msg_len = static_cast<uint32_t>(strlen(header_err_msg));

const std::string nick_already_used_msg = "That nickname is already taken. Please enter a different nickname.";
const std::string nick_change_sucess_msg = "Your nickname has been successfully changed.";
const std::string nick_length_exceed = "The maximum length for the nickname has been exceeded.";

struct Packet {
	PacketHeader header{};
	std::unique_ptr<std::string> payload_up; // 포인터인거 까먹을까봐 이름 이렇게 함

	Packet() : payload_up(std::make_unique<std::string>()) { // 생성자에서 std::string 객체도 생성

	}
};

struct RecvResult {
	NetState state{}; // 수신 과정에서 발생한 상태
	std::shared_ptr<Packet> packet; // 수신한 패킷
};
