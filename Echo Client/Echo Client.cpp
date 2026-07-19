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
const int HEADER_TYPE_SIZE = 4;
const int HEADER_LENGTH_SIZE = 4;
const int HEADER_NICKNAME_SIZE = 32;
const int HEADER_SIZE = HEADER_TYPE_SIZE + HEADER_LENGTH_SIZE + HEADER_NICKNAME_SIZE;

const size_t MAX_NICKNAME_LENGTH = 32;

const char header_err_msg[] = "The maximum value of the header's length field has been exceeded. The client is terminating the connection.";
uint32_t host_err_msg_len = static_cast<uint32_t>(strlen(header_err_msg));

const std::string nick_change_sucess_msg = "Your nickname has been successfully changed.";
const std::string nick_already_used_msg = "That nickname is already taken. Please enter a different nickname.";

using Nickname = std::string;

struct RecvResult {
    NetState state{};
    PacketType type = PacketType::CHAT_MESSAGE;
    uint32_t length = 0;
    Nickname nick;
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

            std::string nickname = input.substr(6); // "/nick "다음 문자열을 nickname으로 복사'

            if (nickname.empty()) {
                parsed_input.valid = false;
                return parsed_input;
            }

            if (nickname.size() > MAX_NICKNAME_LENGTH) {
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
        nick_.resize(MAX_NICKNAME_LENGTH, '\0'); // 시작할 때 기본 닉네임을 빈 닉네임(모든 비트가 0인 32바이트 문자열)로 바꾸는 코드.
    }

    void HandleRecvPacket(const RecvResult& res) {
        switch (res.type) {
            case PacketType::CHAT_MESSAGE:
            {
                LineLogger::GetInstance().WriteChatLog(res.nick, res.payload);

                break;
            }
            case PacketType::HEADER_ERROR:
            {
                LineLogger::GetInstance().WriteChatLog(res.nick, res.payload);
                TryMarkClosing(); // 여기도 서버 코드와 같은 처리 필요할 듯(추측). 아 RemoveThisClient() 같은 코드가 없네. 상관 없겠다.
                break;
            }
            case PacketType::NICKNAME_CHANGE_FAILED:
            {
                LineLogger::GetInstance().WriteChatLog(res.nick, res.payload);
                break;
            }
            // 닉네임 설정 성공은 좀 특수한 케이스로,
            // 페이로드를 출력하지 않고 따로 클라이언트 자체에서 닉네임 설정 성공 메시지를 출력한다.
            // 페이로드는 갱신할 지역 닉네임이다.
            case PacketType::NICKNAME_CHANGE_SUCESS:
            {
                LineLogger::GetInstance().WriteChatLog(res.nick, nick_change_sucess_msg);
                nick_ = res.payload;
            }

            // 다른 패킷 타입은 추후 코드 작성 예정
            // 유효하지 않은 패킷 타입은 이미 RecvPacket() / HandleTransportException()에서 판별 및 처리해줌
        }
            
    }

    void HandleTransportException(const NetState& state) {
        // 다른 스레드가 이미 TryMarkClosing() 한 경우 대비용
        if (!TryMarkClosing()) {
            return;
        }
        // break시 오류 발생 체크, 클라이언트 종료하기
        // 이 로직 별개의 함수로 분리해야함!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        if (state.transport_error) {
            std::cout << "서버와의 통신 과정에서 오류 발생 : ";

            if (state.header_send) std::cout << "헤더 송신 과정에서 오류 발생\n";

            if (state.payload_send) std::cout << "페이로드 송신 과정에서 오류 발생\n";

            if (state.header_recv) std::cout << "헤더 수신 과정에서 오류 발생\n";

            if (state.payload_recv) std::cout << "페이로드 수신 과정에서 오류 발생\n";
        }
        else if (state.protocol_error) {
            std::cout << "서버에서 송신된 헤더의 값이 4096을 초과.\n";

            PacketHeader protocol_err_header;
            protocol_err_header.type = htonl(static_cast<int32_t>(PacketType::HEADER_ERROR));
            protocol_err_header.length = htonl(host_err_msg_len);

            NetState header_err_send_res = SendPacket(header_err_msg, host_err_msg_len, PacketType::HEADER_ERROR, nick_);

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

    // nick은 수정할 닉네임이 아닌 해당 패킷을 송 / 수신하는 주체의 닉네임
    NetState SendPacket(const char* msg, uint32_t len, PacketType type, Nickname nick);

    RecvResult RecvPacket();

    bool TryMarkClosing() {
        bool expected = false;

        if (!closing.compare_exchange_strong(expected, true)) {
            return false;
        }

        return true;
    }

};

void ClientApp::Run() {
    char buf[BUFFER_SIZE];

    // 여기서 유저 입장에서의 초기 닉네임(서버의 user_(session_id)로 표현되는 시스템 입장에서의 초기 닉네임 아님) 할당하는 코드(PacketType::NICKNAME_CHANGE) 작성(입력에 대한 예외처리 포함)
    // 성공하면 break 실패하면 계속 반복
    std::string user_nickname;
    while (true) {
        LineLogger::GetInstance().WriteInputLog("Please enter your nickname (Maximum 32Bytes): ");
        std::getline(std::cin, user_nickname);

        if (user_nickname.size() > MAX_NICKNAME_LENGTH) {
            LineLogger::GetInstance().WriteLog("Please enter a nickname that is 32 bytes or less.");
            continue;
        }
        else if (user_nickname.empty()) {
            LineLogger::GetInstance().WriteLog("Please enter your nickname.. Please.. No Blank!");
            continue;
        }

        NetState res = SendPacket(user_nickname.c_str(), user_nickname.size(), PacketType::NICKNAME_CHANGE, nick_);
        if (res.peer_closed ||
            res.protocol_error ||
            res.transport_error) {

            HandleTransportException(res);
            break;
        }

        RecvResult nickname_change_res = RecvPacket();

        if (nickname_change_res.state.peer_closed ||
            nickname_change_res.state.protocol_error ||
            nickname_change_res.state.transport_error) {

            HandleTransportException(nickname_change_res.state);
            break;
        }

        HandleRecvPacket(nickname_change_res);

        if (nickname_change_res.type == PacketType::NICKNAME_CHANGE_FAILED) {
            LineLogger::GetInstance().WriteLog("Please re-enter your nickname.");
        }

        if (nickname_change_res.type == PacketType::NICKNAME_CHANGE_SUCESS) {
            LineLogger::GetInstance().WriteLog("Default nickname set.");
            break;
        }
    }

    // 초기 닉네임 설정 과정에서 종료해야할 상황이 발생했을 때 종료를 해주는 코드
    // 종료해야할 상황일 때 closing은 다 true로 바꾸고 내려옴
    if (closing == true) {
        return;
    }

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

        NetState SendState = SendPacket(buf, input_to_send.length, input_to_send.type, nick_);

        if (SendState.peer_closed ||
            SendState.peer_protocol_error || 
            SendState.transport_error) {

            HandleTransportException(SendState);
            break;
        }

        RecvResult RecvRes = RecvPacket();

        if (RecvRes.state.peer_closed || 
            RecvRes.state.protocol_error || 
            RecvRes.state.transport_error) {

            HandleTransportException(RecvRes.state);
            break;
        }

        HandleRecvPacket(RecvRes);

        // 여기에 closing == true 체크 추가
        // 위의 HandleRecvPacket() 함수로 인한 종료 상황 발생 시 while break 하는 용도
        // 어차피 HandlePacket() 끝난 후 이 코드가 실행되니 위의 Handle*() 함수들로는 TOCTOU문제는 발생하지 않고, 다음 send()에서 발생함. TOCTOU문제는 그 때 처리.
        if (closing == true) {
            break;
        }

    }

    return;
}

NetState ClientApp::SendPacket(const char* msg, uint32_t len, PacketType type, Nickname nick) {
    
    // 해당 송신 자체의 상태
    NetState send_state{};

    PacketHeader send_host_header{};

    if (len > PAYLOAD_SIZE || len == 0) {
        send_state.protocol_error = true;
        state_.protocol_error = true;
    }

    if (type != PacketType::CHAT_MESSAGE &&
        type != PacketType::HEADER_ERROR &&
        type != PacketType::NICKNAME_CHANGE) {
        send_state.protocol_error = true;
        state_.protocol_error = true;
    }

    // 닉네임 검증은 이미 입력받을 때 보장됨
    // 하지만 무서운걸..ㅋㅋ
    if (nick.size() > MAX_NICKNAME_LENGTH) {
        send_state.protocol_error = true;
        state_.protocol_error = true;
    }

    send_host_header.length = len;

    // 여기까지 왔다면 헤더에는 문제 없음

    // 헤더 직렬화(?)
    send_host_header.type = static_cast<int32_t>(type);

    PacketHeader send_net_header;
    send_net_header.type = htonl(send_host_header.type);
    send_net_header.length = htonl(send_host_header.length);

    // 이 코드에 대한 자세한 내용은 닉네임 시스템 설계 문서 - `PacketHeader::nickname`필드를 설정하는 과정 파트 참조
    // 주석으로 설명하기엔 너무 길다..
    // 결국 이건 닉네임 길이가 가변적이기 때문에 헤더에서 닉네임을 표시하지 않는 바이트는 '\0'으로 패딩 처리하는 코드라 볼 수 있음.
    char nick_buf[MAX_NICKNAME_LENGTH];
    memset(nick_buf, '\0', MAX_NICKNAME_LENGTH);
    memcpy(nick_buf, nick.c_str(), nick.size());
    memcpy(send_net_header.nickname, nick_buf, HEADER_NICKNAME_SIZE); // char*(c스타일 문자열) 이므로 바이트 정렬 신경쓸 필요 없음

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

    // 해당 수신 자체의 상태
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


    // 이것도 닉네임 시스템 설계 문서 참조
    char nick_buf[MAX_NICKNAME_LENGTH + 1]; // 32바이트짜리 닉네임일 경우에도 문자열로 인식하기 위해서 맨 끝에 널문자를 붙이기 위해 +1
    memcpy(nick_buf, recv_net_header.nickname, HEADER_NICKNAME_SIZE);
    nick_buf[MAX_NICKNAME_LENGTH] = '\0'; // 32바이트짜리 닉네임일 경우에도 문자열로 읽을 수 있게 맨 끝에 널문자 붙임. 32바이트보다 닉네임을 표현하는 바이트 수가 적더라도 이미 그 빈 바이트들은 '\0'으로 처리되어있어서 문제 없음

    if (recv_host_header.length > 4096 || recv_host_header.length == 0) {
        state_.protocol_error = true;
        recv_state.protocol_error = true;
        result.state = recv_state;

        return result;
    }

    if (recv_host_header.type != static_cast<int32_t>(PacketType::CHAT_MESSAGE) &&
        recv_host_header.type != static_cast<int32_t>(PacketType::HEADER_ERROR) &&
        recv_host_header.type != static_cast<int32_t>(PacketType::NICKNAME_CHANGE_FAILED) && 
        recv_host_header.type != static_cast<int32_t>(PacketType::NICKNAME_CHANGE_SUCESS)) {
        state_.protocol_error = true;
        recv_state.protocol_error = true;
        result.state = recv_state;

        return result;
    }

    result.type = static_cast<PacketType>(recv_host_header.type);
    result.length = recv_host_header.length;
    result.nick = nick_buf;

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