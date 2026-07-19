# Client Architecture

이 문서는 `MultiThreaded Echo Server`의 **클라이언트**의 전체 구조와 책임 분리를 정리합니다.

현재 프로젝트의 목표는 단순히 여러 클라이언트를 동시에 처리하는 것이 아니라,
**클라이언트 한 명을 하나의 세션 단위로 모델링하고,
그 세션들을 관리하는 서버 구조를 만드는 것**입니다.

---

## 1. 전체 확장 흐름

프로젝트의 확장 흐름은 다음과 같습니다.

```text
기존 단일 클라이언트 Echo Server
        ↓
멀티클라이언트 / 멀티스레드 Echo Server
        ↓
브로드캐스트 Chat Server
```

현재 단계는 두 번째 단계인
**멀티클라이언트 / 멀티스레드 Echo Server의 기본 구조 구현**입니다.

Chat Server 단계는 아직 구현 전이며,
현재는 그 확장을 고려한 구조를 미리 설계하는 중입니다.

---

## 2. 클라이언트의 전체 실행 구조

현재 클라이언트의 실행 구조는 다음과 같습니다.

```text
main()
  ├── WinsockGuard 생성
  ├── ConnectSocket 생성
  ├── 서버 주소 설정 및 ConnectSockConnect() 호출
  ├── ClientApp 생성 (ConnectSocket을 move로 소유권 이전)
  └── ClientApp::Run()
        │
        ├── [초기 닉네임 설정 루프 — 메인 루프 진입 전]
        │     ├── 닉네임 입력 받기
        │     ├── InputParser::Parse() → ParsedInput
        │     ├── NICKNAME_CHANGE 타입이 아니면 안내 메시지 → 루프 재시작
        │     ├── ClientApp::SendPacket() (NICKNAME_CHANGE)
        │     ├── 송신 상태 확인 → 에러 시 종료
        │     ├── ClientApp::RecvPacket()
        │     ├── 수신 상태 확인 → 에러 시 종료
        │     ├── NICKNAME_CHANGE_SUCESS → nick_ 갱신 → break (메인 루프 진입)
        │     └── NICKNAME_CHANGE_FAILED → 실패 원인 출력 → 루프 재시작
        │
        └── [메인 루프]
              ├── 사용자 입력 받기
              ├── InputParser::Parse() → ParsedInput
              ├── valid == false → 에러 출력 → 루프 재시작
              ├── quit == true  → 루프 종료
              ├── length 범위 검사 → 초과 시 에러 출력 → 루프 재시작
              ├── ClientApp::SendPacket() (nick_ 포함)
              ├── 송신 상태 확인 → 에러 시 루프 종료
              ├── ClientApp::RecvPacket()
              ├── 수신 상태 확인 → 에러 시 루프 종료
              └── HandleRecvPacket() → 수신 메시지 출력 또는 닉네임 처리
```

핵심은 `main()`은 소켓 생성과 서버 연결의 구현만 담당하고,
실제 통신 흐름의 구현은 `ClientApp::Run()`에 위임한다는 점입니다.

---

## 3. ClientApp

`ClientApp`은 클라이언트 측의 `ClientSession`에 대응하는 객체입니다.

서버에서 `ClientSession`이 하나의 클라이언트 연결에 대한 socket, 상태, 송수신 흐름을 캡슐화하듯,
`ClientApp`은 클라이언트 자신의 socket, 상태, 통신 흐름을 캡슐화합니다.

### 역할

- `ConnectSocket` 소유 (move로 받음)
- `NetState state_` 소유 (클라이언트 전체 통신 상태 추적)
- `Nickname nick_` 소유 — 생성자에서 32바이트 `'\0'`으로 초기화 (서버 기본 닉네임과 불일치 상태로 시작, 초기 설정 루프에서 동기화)
- `SendPacket()` / `RecvPacket()`으로 패킷 송수신 추상화
- 메인 루프 진입 전 초기 닉네임 설정 루프 실행 — 서버로부터 `NICKNAME_CHANGE_SUCESS` 응답을 받기 전까지 메인 루프에 진입하지 않음
- `Run()`으로 닉네임 설정 → 메인 루프 (입력 → 파싱 → 송신 → 수신 → 처리)의 고수준 흐름 관리
- `HandleTransportException()`으로 종료 / 에러 상황 후처리
- `HandleRecvPacket()`으로 수신한 패킷 처리 (`NICKNAME_CHANGE_SUCESS` / `NICKNAME_CHANGE_FAILED` 포함)

현재 구조는 다음과 같습니다.

```cpp
class ClientApp {
private:
    ConnectSocket sock_;
    NetState state_;
    Nickname nick_;
    std::atomic<bool> closing = false;

public:
    ClientApp(ConnectSocket s);

    void Run();

    NetState SendPacket(const char* msg, uint32_t len, PacketType type);
    RecvResult RecvPacket();

    void HandleRecvPacket(const RecvResult& res);
    void HandleTransportException(const NetState& state);
    bool TryMarkClosing();
};
```

`ClientApp::closing`은 `ClientSession::closing`과 동일한, **논리적 종료 상태**의 의미입니다.

```text
closing == true
  → 이 클라이언트 세션은 논리적 종료 상태다
  → Run()의 while 루프를 종료한다
```

`HandleRecvPacket()`에서 `HEADER_ERROR` 패킷 수신 시 `TryMarkClosing()`을 호출하며,
`Run()`은 `HandleRecvPacket()` 호출 이후 `closing` 상태를 확인하여 루프를 종료합니다.

`ClientApp`에는 서버의 `RemoveThisClient()`에 대응하는 개념이 없으므로
`TryMarkClosing()`의 반환값을 현재는 사용하지 않습니다.
다만 반환 타입을 `bool`로 유지하는 이유는, 추후 클라이언트가 멀티스레드로 확장될 경우를 대비해
"이 호출이 실제로 종료를 성공시켰는가"를 알 수 있는 인터페이스를 미리 갖춰두기 위해서입니다.

초기 닉네임 설정 루프가 필요한 이유는 다음과 같습니다.

```text
클라이언트가 연결한 시점에 서버는 user_(session_id) 기반의 기본 닉네임을 부여하지만,
클라이언트의 nick_은 '\0' × 32로 초기화된 빈 닉네임입니다.

이 상태에서 메인 루프로 진입하면, 클라이언트가 패킷을 보낼 때
헤더의 nickname 필드에 빈 닉네임이 담기게 됩니다.

따라서 연결 직후 닉네임 설정 루프를 통해 서버로부터 NICKNAME_CHANGE_SUCESS를 수신해야만
서버-클라이언트 간 닉네임이 동기화된 상태에서 메인 루프를 시작합니다.
```

`ConnectSocket`은 복사할 수 없으므로, 생성자에서 `std::move()`로 소유권을 받습니다.

```cpp
ConnectSocket connect_sock;
connect_sock.ConnectSockConnect(&server_addr);

ClientApp client(std::move(connect_sock));
client.Run();
```

### `ClientSession`과의 차이점

`ClientApp`은 `ClientSession`을 레퍼런스로 설계하되,
클라이언트에만 해당하는 세 가지 차이점이 있습니다.

```text
입력 출처
  ClientSession → 네트워크 (RecvPacket)
  ClientApp     → 사용자 콘솔 입력

종료 트리거
  ClientSession → transport error / peer exit / protocol error
  ClientApp     → /quit 입력 또는 송수신 에러

유효하지 않은 입력
  ClientSession → 프로토콜 에러 패킷으로 처리
  ClientApp     → 에러 메시지 출력 후 루프 재시작
```

`ClientApp`의 멤버 변수와 핵심 함수에 대한 상세 설계는 [client-component-design.md](client-component-design.md) 참고.

---

## 4. InputParser

### 4-1. InputParser의 필요성

기존 클라이언트는 사용자 입력을 받으면 무조건 일반 메시지로 전송했습니다.

```text
입력 → 전송 (항상 동일한 PacketType)
```

하지만 닉네임 변경처럼 **입력의 종류에 따라 다른 `PacketType`을 보내야 하는 상황**이 생기면서
이 전제가 무너졌습니다.

이를 해결하기 위해 입력 파싱 책임을 `InputParser`로 분리합니다.

```text
InputParser::Parse()
  → "이 입력은 어떤 패킷인가?"를 판단하는 책임만 가짐
  → 실제 송신은 담당하지 않음

ClientApp::SendPacket()
  → 실제 송신 책임만 가짐
  → 입력이 무엇이었는지는 관심 없음
```

`InputParser`는 상태를 가질 필요가 없으므로 `Parse()`를 `static` 함수로 구현합니다.
따라서 `ClientApp` 내부에 소유할 필요가 없습니다.

명령어 식별 방식은 맨 앞의 `/` 유무로 1차 분기하고, 이후 문자열로 2차 분기합니다.

```text
'/'가 없으면 → CHAT_MESSAGE
'/'가 있으면 이후 문자열로 2차 분기
  "/nick " → NICKNAME_CHANGE, payload = prefix 절삭 후 문자열
             단, payload가 32바이트를 초과하면 valid = false
  "/quit"  → quit = true
  "(정의되지 않은 식별자 / 공백)" → valid = false
```

### 4-2. ParsedInput

`ParsedInput`은 `InputParser::Parse()`의 반환 타입입니다.

파싱 결과, 즉 "이 입력으로 무엇을 해야 하는가"를 담아
`ClientApp::Run()`의 송신 로직에 전달하는 데이터 전달 객체입니다.

```cpp
struct ParsedInput {
    PacketType type;     // 보내야 할 패킷 타입
    uint32_t length;     // 보내야 할 페이로드 길이
    std::string payload; // 실제로 보낼 메시지 (식별자 절삭 후)

    bool quit = false;   // 종료 명령인지 (먼저 검사)
    bool valid = true;   // 유효한 입력인지 (먼저 검사)
};
```

입력별 `ParsedInput` 결과는 다음과 같습니다.

```text
"hello"                 → type=CHAT_MESSAGE,    payload="hello", quit=false, valid=true
"/nick maple"           → type=NICKNAME_CHANGE, payload="maple", quit=false, valid=true
"/nick (32바이트 초과)" → valid=false
"/quit"                 → quit=true
""                      → valid=false
"/(정의되지 않은 식별자)" → valid=false
```

`Run()` 내부에서는 반환된 `ParsedInput`을 다음 순서로 검사합니다.

```text
1. valid == false → 에러 메시지 출력 → continue
2. quit == true   → 루프 종료
3. length 범위 검사 → 초과 시 에러 출력 → continue
4. SendPacket(payload, length, type)
```

`InputParser`와 `ParsedInput`에 대한 상세 설계는 [client-component-design.md](client-component-design.md) 참고.

---

## 5. 클라이언트 객체 책임 요약

| 구성 요소 | 책임 |
|---|---|
| `main()` | WinsockGuard 생성, ConnectSocket 생성 및 연결, ClientApp 생성 및 Run() 호출 |
| `ClientApp` | ConnectSocket 소유, 송수신 추상화, 입력 → 파싱 → 송신 → 수신 → 출력 루프 관리 |
| `InputParser` | 사용자 입력 파싱, ParsedInput 반환 |
| `ParsedInput` | 파싱 결과 전달 객체 (type, payload, quit, valid) |
| `ConnectSocket` | raw SOCKET 소유 및 송수신 |
| `NetState` | 송수신 단계와 예외 상태 기록 |

---

## 6. 설계 핵심

현재 구조에서 가장 중요한 기준은 다음입니다.

```text
객체의 책임과 스레드의 책임을 분리한다.
```

즉, 단순히 기능을 클래스로 나누는 것이 아니라,

- 어떤 객체가 어떤 자원을 소유하는지
- 어떤 스레드가 어떤 객체를 사용하는지
- 어떤 객체가 세션의 생명주기를 관리하는지
- 어떤 mutex가 어떤 공유 자원을 보호하는지

를 명확히 나누는 것을 목표로 합니다.
