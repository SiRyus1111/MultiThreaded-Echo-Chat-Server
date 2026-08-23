#pragma once

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "Common.h"
#include <cstdint>
#include "LineLogger.h"

#pragma comment(lib, "ws2_32.lib")

/*
enum class PacketType : int32_t {
    HEADER_ERROR = 0,
    CHAT_MESSAGE = -1
};
*/

enum class PacketType : int32_t {
    // 기본 패킷 타입
    
    // 서버가 수신 / 클라이언트가 송신
    CHAT_MESSAGE = 1, // 전역 브로드캐스트
    NICKNAME_CHANGE = 2, // 닉네임 변경

    // 클라이언트가 수신 / 서버가 송신
    HEADER_ERROR = 3, // 헤더 에러
    NICKNAME_CHANGE_FAILED = 4, // 닉네임 변경 실패
    NICKNAME_CHANGE_SUCESS = 5, // 닉네임 변경 성공
    
    // 룸 관련 패킷 타입

    // 서버가 수신 / 클라이언트가 송신
    JOIN_ROOM = 101, // 룸 입장
    LEAVE_ROOM = 102, // 룸 퇴장
    ROOM_MESSAGE = 103, // 룸 브로드캐스트 / 이건 성공일 때만 전달되기 때문에 실패 패킷 타입을 만들 필요가 없음.
    CREATE_ROOM = 104, // 룸 생성
    DELETE_ROOM = 105, // 룸 삭제

    // 클라이언트가 수신 / 서버가 송신
    // 이 패킷 타입들은 내가 다 생각이 있던거야.
    // 룸에 대한건 성공 / 실패가 나뉠 수 있으니 이렇게 해놓은거.
    JOIN_ROOM_SUCCESS = 201, // 룸 입장 성공
    LEAVE_ROOM_SUCCESS = 202, // 룸 퇴장 성공
    CREATE_ROOM_SUCCESS = 203, // 룸 생성 성공
    DELETE_ROOM_SUCCESS = 204, // 룸 삭제 성공
    JOIN_ROOM_FAILED = 205, // 룸 입장 실패
    LEAVE_ROOM_FAILED = 206, // 룸 퇴장 실패
    CREATE_ROOM_FAILED = 207, // 룸 생성 실패
    DELETE_ROOM_FAILED = 208, // 룸 삭제 실패
    ROOM_DELETED = 209 // 룸 삭제됨
};

#pragma pack(push, 1)
struct PacketHeader {
    int32_t type; // 의미(PacketType)가 아닌 값(int32_t)으로 가짐
    uint32_t length;
    char nickname[32];
};
#pragma pack(pop)

const int32_t HEADER_ERROR = 0;
const int32_t CHAT_MESSAGE = -1;

struct NetState {
    // 진행
    bool header_recv = false;
    bool payload_recv = false;
    bool header_send = false;
    bool payload_send = false;

    // 예외
    bool transport_error = false;
    bool peer_closed = false;
    bool protocol_error = false;
    bool peer_protocol_error = false;
};

// 헤더 규칙
// 첫 4바이트 = int32_t 패킷 타입
// 다음 4바이트 = uint32_t 페이로드 길이
// 만약 패킷 타입의 값이 SERVER_HEADER_ERROR(0)이라면 protocol(Application Layer) error.
// 만약 패킷 타입의 값이 CHAT_MESSAGE(-1)이라면 일반적인 메시지.

inline int send_all(SOCKET sock, NetState& state, const char* msg, int len) {

    int sent_byte = 0;

    while (sent_byte < len) {
        int send_len = send(sock, msg + sent_byte, len - sent_byte, 0);

        if (send_len == SOCKET_ERROR) {
            err_display("send()");
            state.transport_error = true;
            return SOCKET_ERROR;
        }

        sent_byte += send_len;

    }

    return sent_byte;
}

inline int recv_all(SOCKET sock, NetState& state, char* buf, int len) {

    int received_byte = 0;

    while (received_byte < len)
    {
        int recv_len = recv(sock, buf + received_byte, len - received_byte, 0);

        if (recv_len == SOCKET_ERROR) {
            err_display("recv()");
            state.transport_error = true;
            return SOCKET_ERROR;
        }
        else if (recv_len == 0) {
            state.peer_closed = true;
            return 0;
        }

        received_byte += recv_len;

    }

    return received_byte;
}