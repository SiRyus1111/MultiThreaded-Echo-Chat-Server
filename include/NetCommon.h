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
	CHAT_MESSAGE = 1,
    NICKNAME_CHANGE = 2,
    HEADER_ERROR = 3
};

#pragma pack(push, 1)
struct PacketHeader {
    int32_t type;
    uint32_t length;
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