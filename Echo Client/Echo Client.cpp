#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <string>
#include "NetCommon.h"
#include "SocketRAII.h"
#include "LineLogger.h"
#include <atomic>

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

struct RecvResult {
    NetState state{};
    PacketType type = PacketType::CHAT_MESSAGE;
    uint32_t length = 0;
    std::string payload;
};

// 입력받은 문자열의 첫 n글자(식별자)에 따라 클라이언트에서 입력받은 메시지의 동작을 분리

// 입력받은 메시지를 파싱한 결과
struct ParsedInput {
    PacketType type = PacketType::CHAT_MESSAGE; // 오직 보내야할 패킷 타입만 나타냄
    uint32_t length = 0; // 보내야할 페이로드의 길이를 나타냄
    std::string payload; // 실제로 보낼 메시지를 나타냄(/nick같은 메시지 식별자 절삭한)

    bool quit = false; // 종료 메시지냐 (이것 먼저 검사)
    bool valid = true; // 이 파싱된 결과가 유효하냐 (이것 먼저 검사)
};

class InputParser {
private:

public:
    static ParsedInput Parse(const std::string& input) {
        
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
    std::atomic<bool> closing = false; // 해당 클라이언트가 종료해야하는 상태인지 나타내는 플래그(논리적 종료 상태 표현), 현재는 Run() 함수에서 종료해야할 때 while문 break 하는데에 사용
public:
    
    ClientApp(ConnectSocket s) : sock_(std::move(s)) {

    }

    void HandleRecvPacket(const RecvResult& res) {
        switch (res.type) {
            case PacketType::CHAT_MESSAGE:
            {
                LineLogger::GetInstance().WriteLog(res.payload);

                break;
            }
            case PacketType::HEADER_ERROR:
            {
                LineLogger::GetInstance().WriteLog(res.payload);
                MarkClosing();
                break;
            }
            // 다른 패킷 타입은 추후 코드 작성 예정
            // 유효하지 않은 패킷 타입은 이미 RecvPacket() / HandleTransportException()에서 판별 및 처리해줌
        }
            
    }

    void HandleTransportException(const NetState& state) {
        // break시 오류 발생 체크, 클라이언트 종료하기
        // 이 로직 별개의 함수로 분리해야함!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        if (state.transport_error) {
            std::cout << "서버와의 통신 과정에서 오류 발생 : ";

            if (state.header_send) std::cout << "헤더 송신 과정에서 오류 발생\n";

            if (state.payload_send) std::cout << "페이로드 송신 과정에서 오류 발생\n";

            if (state.header_recv) std::cout << "헤더 수신 과정에서 오류 발생\n";

            if (state.payload_recv) std::cout << "페이로드 수신 과정에서 오류 발생";
        }
        else if (state.protocol_error) {
            std::cout << "서버에서 송신된 헤더의 값이 4096을 초과.\n";

            PacketHeader protocol_err_header;
            protocol_err_header.type = htonl(static_cast<int32_t>(PacketType::HEADER_ERROR));
            protocol_err_header.length = htonl(host_err_msg_len);

            NetState header_err_send_res = SendPacket(header_err_msg, host_err_msg_len, PacketType::HEADER_ERROR);

            if (!(!header_err_send_res.peer_closed && !header_err_send_res.peer_protocol_error && !header_err_send_res.protocol_error && !header_err_send_res.transport_error)) {
                HandleTransportException(state);
            }
        }
        /*
        else if (state.peer_protocol_error) {
            std::cout << "서버에서 프로토콜 에러 감지\n";
        }
        */
        else if (state.peer_closed) {
            std::cout << "서버에서 연결 종료\n";
        }
        else {
            std::cout << "정상적으로 연결 종료..\n";
        }

        std::cout << "서버와 연결 종료됨.\n";
    }

    ClientApp& operator=(const ClientApp&) = delete;
    ClientApp(const ClientApp&) = delete;

    void Run();

    NetState SendPacket(const char* msg, uint32_t len, PacketType type);

    RecvResult RecvPacket();

    void MarkClosing() {
        closing.store(true);
        return;
    }

};

void ClientApp::Run() {
    char buf[BUFFER_SIZE];

    while (true) {

        std::string user_input;
        LineLogger::GetInstance().WriteInputLog("Message to send (Maximum 4096 Bytes) : ");
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

            HandleTransportException(SendState);
            MarkClosing();
            break;
        }

        RecvResult RecvRes = RecvPacket();

        if (RecvRes.state.peer_closed || 
            RecvRes.state.protocol_error || 
            RecvRes.state.transport_error) {

            HandleTransportException(RecvRes.state);
            MarkClosing();
            break;
        }

        HandleRecvPacket(RecvRes);

        // 여기에 closing == true 체크 추가
        // 어차피 HandlePacket() 끝난 후 이 코드가 실행되니 위의 Handle*() 함수들로는 TOCTOU문제는 발생하지 않고, 다음 send()에서 발생함. TOCTOU문제는 그 때 처리.
        if (closing == true) {
            break;
        }

    }

    return;
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

RecvResult ClientApp::RecvPacket() {

    char buf[BUFFER_SIZE];

    NetState recv_state{};

    RecvResult result;

    PacketHeader recv_net_header{};

    // 헤더 recv()
    state_.header_recv = true;
    int header_recv_res = sock_.ConnectSockRecv(state_, (char*)&recv_net_header, HEADER_SIZE);

    if (header_recv_res == SOCKET_ERROR) {
        recv_state.transport_error = true;
        result.state = recv_state;

        return result;
    }
        
    if (header_recv_res == 0) {
        recv_state.peer_closed = true;
        result.state = recv_state;

        return result;
    }

    state_.header_recv = false;

    // 헤더 해석
    PacketHeader recv_host_header;
    recv_host_header.type = ntohl(static_cast<int32_t>(recv_net_header.type));
    recv_host_header.length = ntohl(recv_net_header.length);

    if (recv_host_header.length > 4096 || recv_host_header.length == 0) {
        state_.protocol_error = true;
        recv_state.protocol_error = true;
        result.state = recv_state;

        return result;
    }

    if (recv_host_header.type != static_cast<int32_t>(PacketType::CHAT_MESSAGE) &&
        recv_host_header.type != static_cast<int32_t>(PacketType::HEADER_ERROR)) {
        state_.protocol_error = true;
        recv_state.protocol_error = true;
        result.state = recv_state;

        return result;
    }

    result.type = static_cast<PacketType>(recv_host_header.type);
    result.length = recv_host_header.length;

    // 페이로드 recv()
    state_.payload_recv = true;
    int payload_recv_res = sock_.ConnectSockRecv(state_, buf, recv_host_header.length);

    if (payload_recv_res == SOCKET_ERROR) {
        recv_state.transport_error = true;
        result.state = recv_state;

        return result;
    }
        
    if (payload_recv_res == 0) {
        recv_state.peer_closed = true;
        result.state = recv_state;
        
        return result;
    }

    state_.payload_recv = false;

    buf[recv_host_header.length] = '\0';

    result.payload = buf;

    /*
    if (recv_host_header.type == static_cast<int32_t>(PacketType::HEADER_ERROR)) {
        state_.peer_protocol_error = true;
        recv_state.peer_protocol_error = true; // 이걸 안 적어서 버그 발생. 이건 나중에 따로 문서화 하자.
        return recv_state;
    }
    */

    return result;
}

int main() {
    try {
        WinsockGuard winsock;

        ConnectSocket connect_sock;

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