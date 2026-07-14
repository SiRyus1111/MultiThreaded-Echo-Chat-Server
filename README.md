# MultiThreaded Echo Server (WIP: Chat Server) - Winsock2, C++

> 기존 단일 클라이언트 Echo Server를 기반으로,
> 멀티클라이언트 / 멀티스레드 Echo Server로 확장한 뒤,
> 최종적으로 브로드캐스트 기반 Chat Server까지 발전시키는 프로젝트입니다.

> 현재 단계의 목표는 완성된 Chat Server가 아니라,
> **멀티클라이언트 Echo Server를 안정적으로 구성하기 위한 서버 기틀을 만드는 것**입니다.

> 이 프로젝트는 단순히 여러 클라이언트를 동시에 처리하는 데서 끝나지 않고,
> 클라이언트 세션 관리, socket lifetime 관리, 객체 소유권 설계,
> thread-per-client 구조, 공유 컨테이너 동기화, 브로드캐스트 메시지 흐름까지 고려하는 것을 목표로 합니다.

---

## 1. 프로젝트 개요

이 프로젝트는 기존 단일 클라이언트 Echo Server에서 출발합니다.

```text
기존 단일 클라이언트 Echo Server
        ↓
멀티클라이언트 / 멀티스레드 Echo Server
        ↓
브로드캐스트 Chat Server
```

기존 Echo Server 프로젝트에서 구현한 다음 구조를 기반으로 확장합니다.

- TCP byte stream 특성을 고려한 partial send / recv 처리
- `send_all()` / `recv_all()` 기반 송수신 helper
- `PacketHeader(type, length)` 기반 애플리케이션 레벨 프로토콜
- protocol error / transport error / peer exit 구분
- `NetState` 기반 통신 상태 관리
- OOP / RAII 기반 socket 자원 관리

기반 프로젝트:

- [My First Echo Server Project](https://github.com/SiRyus1111/My-First-Echo-Server-Project)

---

## 2. 현재 구현 상태

### 구현 완료

- 기존 단일 클라이언트 Echo Server 로직을 `ClientSession` 기반 구조로 이식
- 클라이언트마다 독립적인 `ClientSession` 객체 생성
- 각 `ClientSession`이 자기 자신의 `ClientSocket`, `ClientAddr`, `ClientAddrStr`, `NetState`, `closing` 상태 소유
- `ClientManager`가 현재 접속 중인 `ClientSession` 목록 관리
- 클라이언트별 `std::thread` 생성
- detached thread가 `shared_ptr<ClientSession>`을 들고 `Run()` 실행
- 종료 상황 발생 시 `ClientSession`이 `ClientManager`에게 자기 자신 제거 요청
- `shared_ptr` / `weak_ptr` / `unique_ptr` 기반 객체 소유 관계 설계
- `ClientSession::RecvPacket()` 1차 구현
- `ClientSession::SendPacket()` 1차 구현
- `Run()`에서 패킷 송수신 세부 로직을 분리하고, Echo Server의 고수준 흐름만 남김
- `PacketType` enum class 도입
- `PacketHeader::type`은 실제 전송 필드로 `int32_t`를 유지하고, `PacketType`은 코드 내부 의미 표현용으로 사용
- payload length가 `0`이거나 `PAYLOAD_SIZE`를 초과하면 protocol error로 처리
- `HandleTransportException()`을 통해 통신 종료 / 예외 후처리 흐름 정리
- `SessionID` 1차 도입
- `LineLogger` 공용 로깅 라이브러리 구현
- 싱글톤 패턴 기반 전역 단일 로거 구조 적용
- 모든 콘솔 출력이 동일한 `std::mutex`를 공유하도록 설계
- `LogType` enum class 기반 로그 타입 제한 구조 구현
- 가변 인자 템플릿(Fold Expression) 기반 로그 조립 기능 구현
- 로그 한 줄 단위 출력 구조 구현
- 완성된 로그 문자열을 한 번의 `std::cout << oss.str()` 연산으로 출력하는 구조 구현
- 출력 구간만 `std::mutex`로 보호하는 최소 범위 동기화 구조 적용
- `SessionID` / `IP:Port` / `LogType` 기반 표준 로그 형식 설계
- `ClientSession` 계층 로그를 `LineLogger` 기반으로 교체
- `ClientSession` 생성 시 `CONNECTED` 로그 출력
- `RecvPacket()` 수신 완료 시 `RECV_COMPLETE` 로그 출력
- `SendPacket()` 송신 완료 시 `SEND_COMPLETE` 로그 출력
- `HandleTransportException(NetState)`에서 종료 / transport error / error packet 송신 로그 출력
- `NetState` 구조체 / `PacketType` 구조체 이름 리팩토링
- `RecvResult` 구조체 구현 (서버 / 클라이언트 공통) — `NetState`, `PacketType`, `length`, `payload` 포함
- `RecvPacket()` 반환값을 `RecvResult`로 변경 — 수신 과정의 성공/실패(`NetState`)와 수신된 패킷의 타입(`PacketType`)을 분리하여 반환
- `HandleRecvPacket()` 구현 (서버 / 클라이언트 공통) — 정상 수신된 패킷을 타입별로 처리하는 패킷 핸들러
- `HandleTransportException()` 책임 재정의 — 수신 과정 자체의 실패만 처리, `HEADER_ERROR` 수신은 `HandleRecvPacket()`으로 이관
- `ClientApp` 클래스 구현 (`ConnectSocket` 캡슐화, `NetState`, `Nickname`, `closing` 소유)
- `ClientApp::HandleRecvPacket()` / `ClientApp::HandleTransportException()` 구현
- `InputParser` / `ParsedInput` 구현
- `LineLogger::WriteInputLog()` 추가 — 줄바꿈 없이 프롬프트를 출력하기 위한 전용 인터페이스 함수
- 닉네임 시스템 도입 - `PacketType` 추가, - `PacketHeader`에 `char nickname[32]` 필드 추가
- 닉네임 시스템 도입에 맞춰 기존 로직 수정 - `RecvPacket()` / `SendPacket()` / `HandleRecvPacket()` `InputParser::Parse()` 수정
- `ECHO_NICK = "EchoFromServer"` 상수 추가 — echo 패킷 헤더 닉네임용
- `SERVER_NICK = "ServerMessage"` 상수 추가 — 서버 알림 패킷 헤더 닉네임용
- `RecvResult`에 `Nickname nick` 필드 추가 — 수신된 패킷의 송신자 닉네임 저장
- `ClientSession` getter 함수 6종 추가 — `GetState()`, `GetClosing()`, `GetSessionID()`, `GetNickname()`, `GetBinaryAddr()`, `GetStrAddr()`
- `ClientManager::GetClients()` 추가 — `clients_mutex` 보호 하에 clients snapshot 반환; `Manager_wp.lock()` 패턴으로 접근
- `LineLogger::WriteSessionLog()` 시그니처에 `std::string nickname` 파라미터 추가; 출력 형식에 `[Nickname name]` 추가
- `LineLogger::WriteChatLog()` 신규 추가 — 클라이언트 수신 메시지 전용 출력 (`[nickname] message` 형식)

### 구현 예정

- `ClientManager` 로그의 `LineLogger` 적용
- `Server` 전역 로그의 `LineLogger` 적용
- low-level transport 계층 로그 정책 검토
- `SessionID` 기반 표준 로그 형식 전면 적용
- ClientSession별 `send_mutex`
- `ClientManager::Broadcast()`
- broadcast 시 clients snapshot 복사 구조
- message type 확장
- broadcast 중 송신 실패 세션 정리 정책
- `SessionID` 기반 로그 출력 및 세션 추적 개선
- `SessionID` 기반 컨테이너 구조 검증

## 3. 핵심 설계 요약

현재 설계의 핵심은 다음과 같습니다.

```text
main thread
  → 새 연결을 받아 ClientSession을 생성하고 client_thread를 시작한다.

client_thread
  → 특정 ClientSession 하나의 실행 흐름을 담당한다.

ClientSession
  → 클라이언트 한 명의 socket, 상태, 송수신 흐름을 관리한다.

ClientManager
  → 여러 ClientSession의 목록과 관계를 관리한다.
```

객체 생존과 종료 상태는 다음처럼 구분합니다.

```text
shared_ptr
  → 객체의 물리적 생존 보장

closing
  → 세션의 논리적 종료 상태

RemoveClient()
  → ClientManager의 관리 목록에서 제거

SessionID
  → ClientManager가 세션을 식별하기 위한 서버 내부 ID
```

이 구분은 이 프로젝트에서 가장 중요한 기준입니다.

객체가 아직 살아있다는 것과,
그 객체가 정상적인 송수신 대상으로 남아있다는 것은 서로 다른 문제입니다.

또한 현재 구조에서는 `ClientManager`가 세션을 제거할 때
`shared_ptr<ClientSession>` 대조가 아니라 `SessionID`를 key로 사용합니다.
이를 위해 `ClientSession`은 자기 자신의 `session_id`를 값으로 저장하고,
`ClientManager`는 `unordered_map<SessionID, shared_ptr<ClientSession>>`로 세션 목록을 관리합니다.

---

## 4. 전체 구조

예상 프로젝트 구조는 다음과 같습니다.

```text
MultiThreaded Echo-Chat Server
├── Server.cpp
├── Client.cpp
├── include
│   ├── Common.h
│   ├── NetCommon.h
│   ├── LineLogger.h
│   └── socketRAII.h
├── README.md
├── IdeaScatch
└── docs
    ├── architecture.md
    ├── protocol.md
    ├── socket-raii.md
    ├── concurrency-design.md
    ├── component-design.md
    ├── roadmap.md
    └── original-design-note.md
```

### 주요 파일

| 파일 | 역할 |
|---|---|
| `Server.cpp` | 서버 실행 코드, accept loop, client thread 생성 |
| `Client.cpp` | 클라이언트 실행 코드, 서버 연결, 메시지 송수신 |
| `Common.h` | 공통 오류 처리 함수 |
| `NetCommon.h` | 패킷 구조, `NetState`, `send_all()`, `recv_all()` |
| `socketRAII.h` | Winsock2 socket 자원 관리 RAII 객체 |
| `LineLogger.h` | Logging을 전담하는 싱글톤 객체 |

---

## 5. 문서 구성

상세 설계 내용은 `docs` 디렉터리로 분리했습니다.

| 문서 | 내용 |
|---|---|
| [`docs/server-architecture.md`](docs/server-architecture.md) | 서버의 스레드 / 클래스의 책임 분리, 전체적인 구조 |
| [`docs/client-architecture.md`](docs/client-architecture.md) | 클라이언트의 스레드 / 클래스의 책임 분리, 전체적인 구조 |
| [`docs/protocol.md`](docs/protocol.md) | TCP byte stream, partial send/recv 처리, 애플리케이션 레벨 프로토콜 관련 |
| [`docs/socket-raii.md`](docs/socket-raii.md) | WinsockGuard, ListenSocket, ClientSocket, ConnectSocket, socket lifetime |
| [`docs/concurrency-design.md`](docs/concurrency-design.md) | 멀티스레딩, 객체 소유권, 세션 생명주기, 동기화 관련 설계 |
| [`docs/server-component-design.md`](docs/server-component-design.md) | 서버의 주요 클래스의 멤버 변수와 핵심 함수의 설계 의도 |
| [`docs/client-component-design.md`](docs/client-component-design.md) | 클라이언트의 주요 클래스의 멤버 변수와 핵심 함수의 설계 의도 |
| [`docs/roadmap.md`](docs/roadmap.md) | 현재 구현 상태, 구현 예정, 향후 개선 계획 |
| [`docs/original-design-note.md`](docs/original-design-note.md) | 기존 README 원본 설계 노트 보존본 |

---

## 6. 패킷 프로토콜 요약

기존 Echo Server 프로젝트의 패킷 구조를 유지합니다.

```text
[PacketHeader][Payload]
```

```cpp
#pragma pack(push, 1)
struct PacketHeader {
    int32_t type;
    uint32_t length;
    char nickname[32];
};
#pragma pack(pop)
```

- `type`: 일반 메시지 / 에러 메시지 / 닉네임 변경 결과 타입 구분
- `length`: payload 길이
- `nickname`: 송신자 닉네임 (고정 32바이트, 가변 길이 닉네임은 0패딩으로 채움)
- 최대 payload 길이: `4096 bytes`
- payload length가 `0`이거나 최대 크기를 초과하면 protocol error로 처리

`PacketHeader::nickname`은 항상 **송신자의 현재 닉네임**입니다.
`NICKNAME_CHANGE` 패킷의 경우 payload에 설정하려는 닉네임이 담기며,
헤더의 `nickname`은 송신한 주체(클라이언트)의 닉네임입니다.

현재 코드에서는 패킷 타입을 의미 있게 표현하기 위해 `PacketType` enum class를 사용합니다.
다만 실제 네트워크 패킷에 들어가는 `PacketHeader::type` 필드는 `int32_t`로 유지합니다.

```cpp
enum class PacketType : int32_t {
    CHAT_MESSAGE = 1,
    NICKNAME_CHANGE = 2,
    HEADER_ERROR = 3,
    NICKNAME_CHANGE_FAILED = 4,
    NICKNAME_CHANGE_SUCESS = 5
};
```

송신 시에는 `PacketType`을 `int32_t`로 변환한 뒤 `htonl()`을 적용하고,
수신 시에는 `ntohl()`로 변환한 정수값을 `PacketType`의 허용 값과 비교합니다.

TCP는 message boundary를 보장하지 않는 byte stream 기반 프로토콜이므로,
`send_all()` / `recv_all()` helper를 통해 요청한 길이만큼 반복 송수신합니다.
현재 `ClientSession`은 이 송수신 절차를 `SendPacket()` / `RecvPacket()`으로 분리하여 관리합니다.

자세한 내용은 [`docs/protocol.md`](docs/protocol.md)를 참고합니다.

## 7. 동기화 설계 요약

추후 Broadcast Chat Server로 확장할 때는 다음 구조를 사용할 예정입니다.

```text
ClientManager의 clients_mutex
  → 현재 접속 중인 ClientSession 목록 보호

ClientSession의 send_mutex
  → 해당 클라이언트에게 보내는 Header + Payload의 패킷 경계 보호
```

핵심은 다음과 같습니다.

```text
보호해야 하는 것은 “브로드캐스트 전체”가 아니라,
“같은 ClientSession에 대한 패킷 송신 순서”이다.
```

따라서 broadcast 시에는 clients 목록을 snapshot으로 복사한 뒤,
`clients_mutex`를 해제하고 각 `ClientSession::SendPacket()`을 호출하는 구조를 목표로 합니다.

자세한 내용은 [`docs/concurrency-design.md`](docs/concurrency-design.md)를 참고합니다.

---

## 8. Logging System 요약 (`LineLogger`)

멀티클라이언트 / 멀티스레드 서버에서는 여러 스레드가 동시에 콘솔에 로그를 출력할 수 있습니다.
기존의 `std::cout << a << b << c;` 방식은 하나의 로그가 여러 번의 출력 연산으로 나뉘기 때문에,
다른 스레드의 출력이 중간에 끼어들면 로그 한 줄의 의미가 깨질 수 있습니다.

이를 줄이기 위해 프로젝트는 공용 로깅 컴포넌트인 `LineLogger`를 사용합니다.

핵심 목표는 다음입니다.

```text
프로젝트의 모든 콘솔 출력 경로를 LineLogger로 단일화하고,
하나의 mutex로 std::cout을 보호하며,
완성된 로그 문자열 하나를 한 번의 출력 연산으로 출력한다.
```

### 설계 의도

- 모든 콘솔 출력을 `LineLogger`를 통해 수행하여 출력 경로를 단일화한다.
- 싱글톤 패턴을 사용해 전역에서 하나의 `LineLogger` 객체만 사용한다.
- 모든 로그 출력이 동일한 `output_mutex_`를 공유하도록 한다.
- 로그 한 줄을 하나의 의미 단위로 취급한다.
- `std::ostringstream`로 로그 문자열을 먼저 완성한 뒤 출력한다.
- 완성된 문자열 하나를 `std::cout << oss.str();` 형태로 한 번에 출력한다.
- 문자열 조립은 lock 밖에서 수행하고, 실제 출력 구간만 lock으로 보호한다.
- `LogType` enum class를 통해 사용할 수 있는 로그 타입을 제한한다.
- `WriteLog()`가 가변 인자 템플릿을 받도록 하여 기존 `std::cout`과 비슷한 감각으로 사용할 수 있게 한다.

### 동작 방식

```text
로그 인자 전달
↓
std::ostringstream로 문자열 조립
↓
mutex 획득
↓
std::cout << 완성된 문자열
↓
mutex 해제
```

이 구조는 다음 효과를 목표로 합니다.

- 로그 단위 일관성 유지
- 출력 연산 횟수 감소
- 콘솔 출력 구간 단순화
- `std::cout` 보호 mutex 단일화
- lock 점유 시간 최소화
- 멀티스레드 환경에서 로그 가독성 향상

### 사용 예시

```cpp
LineLogger::GetInstance().WriteLog(
    "Server started on port ",
    SERVER_PORT
);
```

세션 관련 로그는 다음과 같은 형식으로 출력합니다.

```cpp
LineLogger::GetInstance().WriteSessionLog(
    session_id,
    nickname,
    ClientAddrStr,
    ntohs(ClientAddr.sin_port),
    LineLogger::LogType::RECV_COMPLETE,
    "payload received"
);
```

예상 출력 형식은 다음과 같습니다.

```text
[SessionID 3][Nickname maple][127.0.0.1:53021][RECV_COMPLETE] payload received
```

클라이언트에서 수신 메시지를 출력할 때는 `WriteChatLog()`를 사용합니다.

```cpp
LineLogger::GetInstance().WriteChatLog(nickname, payload);
```

출력 형식은 다음과 같습니다.

```text
[maple] hello world
```

현재 `LineLogger` 라이브러리는 구현 완료 상태이며,
우선 `ClientSession` 계층의 기존 `std::cout` 기반 콘솔 출력을 `LineLogger` 기반으로 교체하였습니다.
추후 low-level Transport 계층의 `std::cout` 기반 콘솔 출력도 `LineLogger` 기반으로 고려하고 있습니다.

---

## 9. 사용 환경

- Windows
- C++
- Winsock2
- Visual Studio

---

## 10. 실행 방법

현재 프로젝트는 기존 Echo Server를 멀티클라이언트 / 멀티스레드 구조로 확장하는 단계입니다.

### 서버 실행

```text
Server.cpp 실행
```

### 클라이언트 실행

```text
Client.cpp 실행
```

### 여러 클라이언트 접속

여러 개의 클라이언트를 실행하여 서버에 접속합니다.

### Echo 단계

각 클라이언트는 자신이 보낸 메시지를 서버로부터 다시 수신합니다.

```text
Client A → Server → Client A
```

### Chat 단계

브로드캐스트 기능이 추가되면,
한 클라이언트가 보낸 메시지를 다른 클라이언트들이 수신합니다.

```text
Client A → Server → Client B
                  → Client C
                  → Client D
```

---

## 11. 향후 계획 요약

단기 목표:

- 로그 출력 형식 개선 (`LineLogger` 전면 적용)
- `ClientSession`별 `send_mutex` 추가
- `ClientManager::Broadcast()` 구현

중기 목표:

- Broadcast Chat Server 구현
- message type 확장 (`JOIN`, `LEAVE`, `SERVER_NOTICE` 등)
- 송신 실패 세션 정리 정책 구현

장기 목표:

- detach 기반 종료 정책 검증
- send queue 구조 검토
- select() 기반 멀티플렉싱 서버로 확장 가능성 검토
