#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <string>
#include "NetCommon.h"
#include "SocketRAII.h"

const char* SERVER_ADDR = "127.0.0.1";
const int SERVER_PORT = 9000;
const int BUFFER_SIZE = 4096;
const int HEADER_SIZE = 8;
const int HEADER_TYPE_SIZE = 4;
const int HEADER_LENGTH_SIZE = 4;

const char header_err_msg[] = "[CLIENT] 헤더의 최댓값 초과됨. 클라이언트에서 연결을 종료합니다.";
uint32_t host_err_msg_len = static_cast<uint32_t>(strlen(header_err_msg));

int main() {
    try {
        WinsockGuard winsock;

        ConnectSocket connect_sock;
        char buf[BUFFER_SIZE + 1];

        sockaddr_in server_addr{};

        server_addr.sin_family = AF_INET;
        inet_pton(AF_INET, SERVER_ADDR, &server_addr.sin_addr);
        server_addr.sin_port = htons(SERVER_PORT);

        connect_sock.ConnectSockConnect(&server_addr);

        NetState client_state;

        while (true) {

            std::string user_input;
            std::cout << "서버에 보낼 메시지 입력(최대 4096바이트, 서버와 연결 종료 희망시 quit 입력) : ";
            std::getline(std::cin, user_input);

            if (user_input == "quit") break;

            PacketHeader send_host_header;
            // 입력 받은 바이트 수 세기
            send_host_header.length = user_input.size();
            if (send_host_header.length > BUFFER_SIZE) {
                std::cout << "헤더의 값이 최대 버퍼 크기인 4096(바이트)을 초과. 다시 메시지를 입력해주세요.\n";
                continue;
            }

            // 여기까지 왔다면 헤더에는 문제 없음

            // 헤더 직렬화(?)
            memcpy(buf, user_input.c_str(), send_host_header.length);
            send_host_header.type = SAFE;

            PacketHeader send_net_header;
            send_net_header.type = htonl(send_host_header.type);
            send_net_header.length = htonl(send_host_header.length);

            // 헤더 send()
            client_state.header_send = true;
            int header_send_res = connect_sock.ConnectSockSend(client_state, (char*)&send_net_header, HEADER_SIZE);

            if (header_send_res == SOCKET_ERROR) break;

            client_state.header_send = false;

            // 페이로드 send()
            client_state.payload_send = true;
            int payload_send_res = connect_sock.ConnectSockSend(client_state, buf, send_host_header.length);

            if (payload_send_res == SOCKET_ERROR) break;

            client_state.payload_send = false;

            PacketHeader recv_net_header;

            // 헤더 recv()
            client_state.header_recv = true;
            int header_recv_res = connect_sock.ConnectSockRecv(client_state, (char*)&recv_net_header, HEADER_SIZE);

            if (header_recv_res == SOCKET_ERROR || header_recv_res == 0) break;

            client_state.header_recv = false;

            // 헤더 해석
            PacketHeader recv_host_header;
            recv_host_header.type = ntohl(recv_net_header.type);
            recv_host_header.length = ntohl(recv_net_header.length);

            if (recv_host_header.length > 4096) {
                client_state.if_header_error = true;
                break;
            }

            std::cout << "헤더 해석 완료. 페이로드를 수신합니다.\n";

            // 페이로드 recv()
            client_state.payload_recv = true;
            int payload_recv_res = connect_sock.ConnectSockRecv(client_state, buf, recv_host_header.length);

            if (payload_recv_res == SOCKET_ERROR || payload_recv_res == 0) break;

            client_state.payload_recv = false;

            buf[recv_host_header.length] = '\0';

            std::cout << "[ECHO FROM SERVER]" << buf << '\n';

            if (recv_host_header.type == HEADER_ERROR) {
                client_state.if_peer_error = true;
                break;
            }
        }
        // break시 오류 발생 체크, 클라이언트 종료하기
        if (client_state.if_error) {
            std::cout << "서버와의 통신 과정에서 오류 발생 : ";

            if (client_state.header_send) std::cout << "헤더 송신 과정에서 오류 발생\n";

            if (client_state.payload_send) std::cout << "페이로드 송신 과정에서 오류 발생\n";

            if (client_state.header_recv) std::cout << "헤더 수신 과정에서 오류 발생\n";

            if (client_state.payload_recv) std::cout << "페이로드 수신 과정에서 오류 발생";
        }
        else if (client_state.if_header_error) {
            std::cout << "서버에서 송신된 헤더의 값이 4096을 초과.\n";

            PacketHeader protocol_err_header;
            protocol_err_header.type = htonl(HEADER_ERROR);
            protocol_err_header.length = htonl(host_err_msg_len);

            int header_err_send_res = connect_sock.ConnectSockSend(client_state, (char*)&protocol_err_header, HEADER_SIZE);
            if (header_err_send_res == SOCKET_ERROR) {
                std::cout << "헤더 오류 메시지 서버에 전송 실패.\n";
            }
            else {
                int err_send_res = connect_sock.ConnectSockSend(client_state, header_err_msg, host_err_msg_len);
                if (err_send_res == SOCKET_ERROR) {
                    std::cout << "헤더 오류 메시지 서버에 전송 실패.\n";
                }
            }
        }
        else if (client_state.if_peer_error) {

        }
        else if (client_state.if_peer_exit) {
            std::cout << "서버에서 연결 종료\n";
        }
        else {
            std::cout << "정상적으로 연결 종료..\n";
        }

        std::cout << "서버와 연결 종료됨.\n";
    }
    catch (std::exception& e) {
        std::cerr << e.what() << '\n';
    }

    return 0;
}