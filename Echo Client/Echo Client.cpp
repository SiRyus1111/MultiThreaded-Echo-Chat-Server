#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <string>
#include "NetCommon.h"
#include "SocketRAII.h"
#include "LineLogger.h"

const char* SERVER_ADDR = "127.0.0.1";
const int SERVER_PORT = 9000;
const int PAYLOAD_SIZE = 4096;
const int BUFFER_SIZE = PAYLOAD_SIZE + 1;
const int HEADER_SIZE = 8;
const int HEADER_TYPE_SIZE = 4;
const int HEADER_LENGTH_SIZE = 4;

const char header_err_msg[] = "The maximum value of the header's length field has been exceeded. The client is terminating the connection.";
uint32_t host_err_msg_len = static_cast<uint32_t>(strlen(header_err_msg));

using Nickname = std::string;

// 입력받은 문자열의 첫 n글자(식별자)에 따라 클라이언트에서 입력받은 메시지의 동작을 분리

// 입력받은 메시지를 파싱한 결과
struct ParsedInput {
    PacketType type; // 오직 보내야할 패킷 타입만 나타냄
    uint32_t length; // 보내야할 페이로드의 길이를 나타냄
    std::string payload; // 실제로 보낼 메시지를 나타냄(/nick같은 메시지 식별자 절삭한)

    bool quit = false; // 종료 메시지냐 (이것 먼저 검사)
    bool valid = true; // 이 파싱된 결과가 유효하냐 (이것 먼저 검사)
};

class InputParser {
private:

public:
    static ParsedInput Parse(std::string& input) {
        
        ParsedInput parsed_input;

        if (input.empty()) {
            parsed_input.valid = false;
            return parsed_input;
        }
        
        if (input[0] != '/') { // 일반 메시지인 경우   

            parsed_input.type = PacketType::CHAT_MESSAGE;

            parsed_input.payload = input;

            parsed_input.length = input.size();

            return parsed_input;
        }

        // 식별자를 봐야하는 메시지인 경우

        if (input == "/quit") { // 종료
            parsed_input.quit = true;
            return parsed_input;
        }

        if (input.starts_with("/nick ")) { // 닉네임 변경

            std::string nickname = input.substr(6); // "/nick "다음 문자열을 nickname으로 복사
            if (nickname.empty()) {
                parsed_input.valid = false;
                return parsed_input;
            }

            parsed_input.type = PacketType::NICKNAME_CHANGE;
            parsed_input.payload = nickname;
            parsed_input.length = nickname.size();

            return parsed_input;
        }

        // 추후에 다른 식별자 추가 가능

        // 여기까지 오려면 input이 !(일반 메시지 || /nick으로 시작하는 메시지 || /quit으로 시작하는 메시지)여야 함.
        // 즉, 유효하지 않은 메시지.
        parsed_input.valid = false;
        return parsed_input;
    }
};

class ClientApp {
private:
    ConnectSocket sock_;
    NetState state_; // 클라이언트 전체 상태 (생애주기 추적용)
    // send_state / recv_state는 각 송수신 호출의 결과 스냅샷
    Nickname nick_;
public:
    
    ClientApp(ConnectSocket s) : sock_(std::move(s)) {

    }

    ClientApp operator=(ClientApp&) = delete;
    ClientApp(ClientApp&) = delete;

    void Run();

    NetState SendPacket(const char* msg, uint32_t len, PacketType type);

    NetState RecvPacket(char* buf);
};

void ClientApp::Run() {
    char buf[BUFFER_SIZE];

    while (true) {

        std::string user_input;
        LineLogger::GetInstance().WriteLog("Message to send (Maximum 4096 Bytes) : ");
        std::getline(std::cin, user_input);

        ParsedInput input_to_send = InputParser::Parse(user_input);

        if (input_to_send.valid == false) {
            LineLogger::GetInstance().WriteLog("The message you entered is invalid. Please re - enter the message.\n");
            continue;
        }
        if (input_to_send.quit == true) {
            LineLogger::GetInstance().WriteLog("\"quit\" has been entered. The client is shutting down.");
            break;
        }

        if (input_to_send.length > PAYLOAD_SIZE || input_to_send.length == 0) {
            LineLogger::GetInstance().WriteLog("The message you entered exceeds ", PAYLOAD_SIZE, "bytes. Please re - enter the message.\n");
            continue;
        }

        memcpy(buf, input_to_send.payload.c_str(), input_to_send.length);

        NetState SendState = SendPacket(buf, input_to_send.length, input_to_send.type);

        if (SendState.peer_closed ||
            SendState.peer_protocol_error || 
            SendState.transport_error) {
            break;
        }

        NetState RecvState = RecvPacket(buf);

        if (RecvState.peer_closed || 
            RecvState.peer_protocol_error || 
            RecvState.protocol_error || 
            RecvState.transport_error) {
            break;
        }

        LineLogger::GetInstance().WriteLog("EchoFromServer : ", buf);
    }
    // break시 오류 발생 체크, 클라이언트 종료하기
    if (state_.transport_error) {
        std::cout << "서버와의 통신 과정에서 오류 발생 : ";

        if (state_.header_send) std::cout << "헤더 송신 과정에서 오류 발생\n";

        if (state_.payload_send) std::cout << "페이로드 송신 과정에서 오류 발생\n";

        if (state_.header_recv) std::cout << "헤더 수신 과정에서 오류 발생\n";

        if (state_.payload_recv) std::cout << "페이로드 수신 과정에서 오류 발생";
    }
    else if (state_.protocol_error) {
        std::cout << "서버에서 송신된 헤더의 값이 4096을 초과.\n";

        PacketHeader protocol_err_header;
        protocol_err_header.type = htonl(static_cast<int32_t>(PacketType::HEADER_ERROR));
        protocol_err_header.length = htonl(host_err_msg_len);

        int header_err_send_res = sock_.ConnectSockSend(state_, (char*)&protocol_err_header, HEADER_SIZE);
        if (header_err_send_res == SOCKET_ERROR) {
            std::cout << "헤더 오류 메시지 서버에 전송 실패.\n";
        }
        else {
            int err_send_res = sock_.ConnectSockSend(state_, header_err_msg, host_err_msg_len);
            if (err_send_res == SOCKET_ERROR) {
                std::cout << "헤더 오류 메시지 서버에 전송 실패.\n";
            }
        }
    }
    else if (state_.peer_protocol_error) {
        std::cout << "서버에서 프로토콜 에러 감지\n";
    }
    else if (state_.peer_closed) {
        std::cout << "서버에서 연결 종료\n";
    }
    else {
        std::cout << "정상적으로 연결 종료..\n";
    }

    std::cout << "서버와 연결 종료됨.\n";
}

NetState ClientApp::SendPacket(const char* msg, uint32_t len, PacketType type) {
    
    NetState send_state{};


    PacketHeader send_host_header{};

    send_host_header.length = len;

    // 여기까지 왔다면 헤더에는 문제 없음

    // 헤더 직렬화(?)
    send_host_header.type = static_cast<int32_t>(type);

    PacketHeader send_net_header;
    send_net_header.type = htonl(send_host_header.type);
    send_net_header.length = htonl(send_host_header.length);

    // 헤더 send()
    state_.header_send = true;
    send_state.header_send = true;
    int header_send_res = sock_.ConnectSockSend(state_, (char*)&send_net_header, HEADER_SIZE);

    if (header_send_res == SOCKET_ERROR) {
        send_state.transport_error = true;
        return send_state;
    }

    state_.header_send = false;
    send_state.header_send = false;

    // 페이로드 send()
    state_.payload_send = true;
    send_state.payload_send = true;
    int payload_send_res = sock_.ConnectSockSend(state_, msg, send_host_header.length);

    if (payload_send_res == SOCKET_ERROR) {
        send_state.transport_error = true;
        return send_state;
    }

    state_.payload_send = false;
    send_state.payload_send = false;

    return send_state;
}

NetState ClientApp::RecvPacket(char* buf) {

    NetState recv_state{};

    PacketHeader recv_net_header{};

    // 헤더 recv()
    state_.header_recv = true;
    int header_recv_res = sock_.ConnectSockRecv(state_, (char*)&recv_net_header, HEADER_SIZE);

    if (header_recv_res == SOCKET_ERROR) {
        recv_state.transport_error = true;
        return recv_state;
    }
        
    if (header_recv_res == 0) {
        recv_state.peer_closed = true;
        return recv_state;
    }

    state_.header_recv = false;

    // 헤더 해석
    PacketHeader recv_host_header;
    recv_host_header.type = ntohl(static_cast<int32_t>(recv_net_header.type));
    recv_host_header.length = ntohl(recv_net_header.length);

    if (recv_host_header.length > 4096) {
        state_.protocol_error = true;
        recv_state.protocol_error = true;
        return recv_state;
    }

    // 페이로드 recv()
    state_.payload_recv = true;
    int payload_recv_res = sock_.ConnectSockRecv(state_, buf, recv_host_header.length);

    if (payload_recv_res == SOCKET_ERROR) {
        recv_state.transport_error = true;
        return recv_state;
    }
        
    if (payload_recv_res == 0) {
        recv_state.peer_closed = true;
        return recv_state;
    }

    state_.payload_recv = false;

    buf[recv_host_header.length] = '\0';

    if (recv_host_header.type == static_cast<int32_t>(PacketType::HEADER_ERROR)) {
        state_.peer_protocol_error = true;
        recv_state.peer_protocol_error = true;
        return recv_state;
    }

    return recv_state;
}

int main() {
    try {
        WinsockGuard winsock;

        ConnectSocket connect_sock;
        char buf[BUFFER_SIZE];

        sockaddr_in server_addr{};

        server_addr.sin_family = AF_INET;
        inet_pton(AF_INET, SERVER_ADDR, &server_addr.sin_addr);
        server_addr.sin_port = htons(SERVER_PORT);

        connect_sock.ConnectSockConnect(&server_addr);

        LineLogger::GetInstance().WriteLog("Connected to the server.");

        ClientApp client(std::move(connect_sock));

        client.Run();
    }
    catch (std::exception& e) {
        std::cerr << e.what() << '\n';
    }

    return 0;
}