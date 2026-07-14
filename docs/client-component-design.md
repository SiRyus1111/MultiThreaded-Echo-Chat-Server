# Client Component Design

이 문서는 `MultiThreaded Echo Server`의 주요 클래스, 멤버 변수, 핵심 함수가
어떤 책임과 설계 의도를 가지고 구성되었는지 정리합니다.

이 문서는 완전한 내부 구현 설명서가 아닙니다.

목표는 다음입니다.

```text
각 클래스와 함수가 왜 존재하는지,
어떤 책임을 가지는지,
다른 구성 요소와 어떻게 연결되는지 설명한다.
```

따라서 실제 구현의 세부 코드보다는,
프로젝트 구조를 이해하는 데 필요한 설계 의도 중심으로 정리합니다.

---

## 1. 문서의 위치

현재 프로젝트 문서의 역할은 다음과 같이 나눕니다.

```text
README.md
  → 프로젝트 대표 소개, 핵심 요약, 실행 방법, 문서 링크

docs/server-architecture.md
  → 서버의 전체 구조와 책임 분리

docs/client-architecture.md
  → 클라이언트의 전체 구조와 책임 분리

docs/protocol.md
  → 패킷 구조, TCP byte stream, 송수신 흐름

docs/socket-raii.md
  → Winsock2 socket 자원 수명 관리

docs/concurrency-design.md
  → 멀티스레딩, 객체 소유권, 세션 생명주기, 동기화 설계

docs/roadmap.md
  → 구현 완료 / 구현 예정 / 향후 개선 계획

docs/server-component-design.md
  → 서버 주요 클래스의 멤버 변수와 핵심 함수의 설계 의도

docs/client-component-design.md
  → 클라이언트 주요 클래스의 멤버 변수와 핵심 함수의 설계 의도
```

이 문서는 클라이언트 쪽 컴포넌트의 설계 의도를 다룹니다. 서버 쪽 컴포넌트는 [server-component-design.md](server-component-design.md)에서 다룹니다.

즉, 이 문서는 `client-architecture.md`와 `protocol.md` 사이를 이어주는 문서입니다.

`client-architecture.md`가 "전체적으로 누가 어떤 책임을 가지는가?"를 설명한다면,
이 문서는 "그 책임을 수행하기 위해 각 클래스와 함수가 왜 이런 형태를 가지는가?"를 설명합니다.

---

## 2. 설계 기준

이 프로젝트의 컴포넌트 설계 기준은 다음과 같습니다.

```text
1. 클라이언트의 동작은 ClientApp 하나로 추상화한다.
2. 사용자 입력 파싱 책임은 InputParser로 분리한다.
3. InputParser는 파싱 결과만 반환하고, 실제 송신은 담당하지 않는다.
```

서버 쪽 설계 기준은 [server-component-design.md](server-component-design.md) §2 참고.

---

# 3. ClientApp

## 3-1. 역할

`ClientApp`은 클라이언트 측의 최상위 객체입니다.

서버의 `ClientSession`이 하나의 클라이언트 연결에 대한 socket, 상태, 송수신 흐름을 캡슐화하듯,
`ClientApp`은 클라이언트 자신의 socket, 상태, 통신 흐름을 캡슐화합니다.

```text
ClientApp
  → 해당 클라이언트의 자원과 동작을 모아놓은 객체

InputParser
  → 입력을 파싱해서 ParsedInput을 반환하는 객체

ParsedInput
  → 파싱된 입력, 즉 해당 입력으로 처리해야 하는 작업을 나타내는 객체
```

이 장에서 다루는 `NetState`, `PacketType`은 [server-component-design.md](server-component-design.md)에서 정의하는 개념을 그대로 사용합니다. 각 개념의 상세 설명은 해당 문서 §10, §11 참고.

---

## 3-2. 소유 관계

```cpp
class ClientApp {
private:
    ConnectSocket sock_;  // 서버와 TCP 연결로 통신하는 소켓
    NetState state_;      // 클라이언트 전체 상태 (생애주기 추적용)
    Nickname nick_;       // 추후 닉네임 시스템 연동 시 사용
    std::atomic<bool> closing = false; // 논리적 종료 상태 표시
};
```

`ConnectSocket`은 복사가 불가능한 RAII 객체이므로,
생성자에서 `std::move()`로 소유권을 받습니다.

```text
ClientApp
  └── ConnectSocket sock_
        └── SOCKET
```

`state_`는 `ClientSession`의 `ClientState`와 동일한 역할입니다.

```text
state_      → ClientApp 전체 누적 통신 상태
send_state  → 이번 SendPacket() 호출의 결과만
recv_state  → 이번 RecvPacket() 호출의 결과만
```

이 상태 기록 정책은 `ClientSession::SendPacket()` / `ClientSession::RecvPacket()`과 동일합니다.

---

## 3-3. Run()

`Run()`은 클라이언트의 고수준 실행 루프를 담당합니다.

```text
[초기 닉네임 설정 루프 — 메인 루프 진입 전]
while(true)
  ├── 닉네임 입력 받기
  ├── 닉네임을 입력하지 않았거나 32바이트를 초과한다면 안내 메시지 → continue
  ├── SendPacket(payload, length, NICKNAME_CHANGE, nick_)
  ├── 송신 상태 확인 → 에러 시 종료
  ├── RecvPacket()
  ├── 수신 상태 확인 → 에러 시 종료
  ├── HandleRecvPacket()
  │     ├── NICKNAME_CHANGE_SUCESS → nick_ = res.payload → break
  │     └── NICKNAME_CHANGE_FAILED → 실패 원인 출력 → continue
  └── (break 발생 시 메인 루프 진입)

[메인 루프]
while(true)
  ├── 사용자 입력 받기
  ├── InputParser::Parse() → ParsedInput
  ├── valid == false → 에러 출력 → continue
  ├── quit == true  → 루프 종료
  ├── length 범위 초과 → 에러 출력 → continue
  ├── SendPacket(payload, length, type, nick_)
  ├── 송신 상태 확인 → 에러 시 루프 종료
  ├── RecvPacket()
  ├── 수신 상태 확인 → 에러 시 루프 종료
  └── HandleRecvPacket() → 수신 메시지 출력 또는 닉네임 처리
```

`Run()`은 입력 파싱의 세부 절차를 직접 알 필요가 없습니다.
`InputParser::Parse()`가 반환한 `ParsedInput`을 보고
어떤 패킷을 보낼지, 루프를 계속할지 결정합니다.

## 3-4. HandleRecvPacket()

```cpp
void HandleRecvPacket(const RecvResult& res);
```

`ClientApp::HandleRecvPacket()`은 `RecvPacket()`이 정상적으로 수신한 패킷을
타입별로 처리하는 패킷 핸들러입니다.

이 함수는 `RecvResult.state`에 이상이 없는 경우, 즉 수신 자체가 성공한 경우에만 호출됩니다.

```text
switch (res.type)
  CHAT_MESSAGE
    → WriteChatLog(res.nick, res.payload)로 수신 메시지 출력

  HEADER_ERROR
    → WriteChatLog(res.nick, 서버 종료 알림 메시지)
    → MarkClosing()

  NICKNAME_CHANGE_SUCESS
    → nick_ = res.payload (서버가 응답한 payload에 새 닉네임 포함)
    → WriteChatLog()로 성공 메시지 출력

  NICKNAME_CHANGE_FAILED
    → WriteChatLog()로 실패 원인 메시지 출력
```

#### ClientSession::HandleRecvPacket()과의 차이점

서버의 `ClientSession::HandleRecvPacket()`과 이 함수는 구조는 동일하지만,
수신할 수 있는 `PacketType`이 다릅니다.

```text
ClientSession (서버)
  → CHAT_MESSAGE, HEADER_ERROR, NICKNAME_CHANGE 처리

ClientApp (클라이언트)
  → CHAT_MESSAGE, HEADER_ERROR, NICKNAME_CHANGE_SUCESS, NICKNAME_CHANGE_FAILED 처리
```

클라이언트는 서버에 `NICKNAME_CHANGE`를 보내는 측이므로 `NICKNAME_CHANGE`를 수신하지 않습니다.
대신 서버의 처리 결과인 `NICKNAME_CHANGE_SUCESS` / `NICKNAME_CHANGE_FAILED`를 수신합니다.

`HandleTransportException()`과의 책임 구분은 `ClientSession`과 동일합니다.

```text
HandleTransportException()
  → 수신 과정 자체의 실패 처리 (transport error / peer exit / protocol error)

HandleRecvPacket()
  → 정상 수신된 패킷의 타입별 처리
```

---

## 3-5. WriteChatLog()

`WriteChatLog()`는 `LineLogger`의 멤버 함수입니다. `LineLogger`의 싱글톤 구조, `WriteLog()` 등 기본 개념은 [server-component-design.md](server-component-design.md) §8 참고.

```cpp
template<typename... Args>
void WriteChatLog(std::string nickname, Args&&... args);
```

`WriteChatLog()`는 클라이언트가 수신한 메시지를 콘솔에 출력하기 위한 전용 함수입니다.

출력 형식은 다음과 같습니다.

```text
[maple] hello world
[EchoFromServer] hello world
```

서버 세션 로그(`WriteSessionLog()`)와 달리 `SessionID`, `IP:Port`, `LogType`이 없습니다.
이 함수는 서버 로그가 아닌 **클라이언트가 사용자에게 보여주는 채팅 메시지 출력**에만 사용합니다.

사용 예시는 다음과 같습니다.

```cpp
// CHAT_MESSAGE 수신 시
LineLogger::GetInstance().WriteChatLog(res.nick, res.payload);

// HEADER_ERROR 수신 시 (서버 알림 메시지)
LineLogger::GetInstance().WriteChatLog(res.nick, "서버가 연결을 종료합니다.");
```

---

## 3-6. InputParser / ParsedInput

### InputParser

`InputParser`는 사용자 입력을 받아 "이 입력으로 무엇을 해야 하는가"만 판단합니다.
실제 송신은 전혀 관여하지 않습니다.

상태를 가질 필요가 없으므로 `Parse()`를 `static` 함수로 구현합니다.

```cpp
class InputParser {
public:
    static ParsedInput Parse(std::string& input);
};
```

`InputParser::Parse()` 함수의 결과 예시 : 

```text
"hello"                 → type=CHAT_MESSAGE,    payload="hello", quit=false, valid=true
"/nick maple"           → type=NICKNAME_CHANGE, payload="maple", quit=false, valid=true
"/nick (32바이트 초과 문자열)" → valid=false
"/quit"                 → quit=true
""                      → valid=false
"/(정의되지 않은 식별자)" → valid=false
```

### ParsedInput

`ParsedInput`은 `Parse()`의 결과를 담아 송신 로직에 전달하는 데이터 전달 객체입니다.

```cpp
struct ParsedInput {
    PacketType type;
    uint32_t length;
    std::string payload;

    bool quit = false;
    bool valid = true;
};
```

`quit`와 `valid`는 `type`보다 먼저 검사합니다.
`valid == false`이거나 `quit == true`인 경우 `type`과 `payload`는 의미가 없습니다.

---

# 4. 정리

현재 컴포넌트 설계의 핵심은 다음과 같습니다.

```text
ClientApp
  → 클라이언트의 자원과 동작을 캡슐화한다.

InputParser
  → 사용자 입력을 파싱하고 ParsedInput을 반환한다.
  → 실제 송신은 담당하지 않는다.

ParsedInput
  → 파싱 결과를 담아 송신 로직에 전달한다.
```

서버 쪽 정리는 [server-component-design.md](server-component-design.md) §13 참고.
