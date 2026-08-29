#pragma once

#include "Types.h"

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
    InputParser() = default;
    ~InputParser() = default;

    static ParsedInput ParseQuitInput() {
        ParsedInput parsed_input;
        parsed_input.quit = true;
        return parsed_input;
    }

    static ParsedInput ParseNicknameChangeInput(std::string input) {
        ParsedInput parsed_input;

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
public:
    InputParser(const InputParser& i) = delete;
    InputParser& operator=(const InputParser& i) = delete;

    static ParsedInput Parse(const std::string& input)  {

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
            return ParseQuitInput();
        }
        else if (input.starts_with("/nick ")) { // 닉네임 변경
            return ParseNicknameChangeInput(input);
        }

        // 추후에 다른 식별자 추가 가능

        // 여기까지 오려면 input이 !(일반 메시지 || /nick으로 시작하는 메시지 || /quit으로 시작하는 메시지)여야 함.
        // 즉, 유효하지 않은 메시지.
        parsed_input.valid = false;
        return parsed_input;
    }
};