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
- `TransportExceptionHandling()`을 통해 통신 종료 / 예외 후처리 흐름 정리

### 구현 예정

- `SessionID`
- 로그 출력 형식 개선
- ClientSession별 `send_mutex`
- `ClientManager::Broadcast()`
- broadcast 시 clients snapshot 복사 구조
- nickname / message type 확장
- broadcast 중 송신 실패 세션 정리 정책

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
```

이 구분은 이 프로젝트에서 가장 중요한 기준입니다.

객체가 아직 살아있다는 것과,
그 객체가 정상적인 송수신 대상으로 남아있다는 것은 서로 다른 문제입니다.

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

---

## 5. 문서 구성

상세 설계 내용은 `docs` 디렉터리로 분리했습니다.

| 문서 | 내용 |
|---|---|
| [`docs/architecture.md`](docs/architecture.md) | main thread, client thread, ClientSession, ClientManager 등 스레드 / 클래스의 책임 분리, 전체적인 구조 |
| [`docs/protocol.md`](docs/protocol.md) | TCP byte stream, partial send/recv 처리, 애플리케이션 레벨 프로토콜 관련 |
| [`docs/socket-raii.md`](docs/socket-raii.md) | WinsockGuard, ListenSocket, ClientSocket, ConnectSocket, socket lifetime |
| [`docs/concurrency-design.md`](docs/concurrency-design.md) | 멀티스레딩, 객체 소유권, 세션 생명주기, 동기화 관련 설계 |
| [`docs/component-design.md`](docs/component-design.md) | 주요 클래스의 멤버 변수와 핵심 함수의 설계 의도 |
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
};
#pragma pack(pop)
```

- `type`: 일반 메시지 / 에러 메시지 / 추후 채팅 메시지 타입 구분
- `length`: payload 길이
- 최대 payload 길이: `4096 bytes`
- payload length가 `0`이거나 최대 크기를 초과하면 protocol error로 처리

현재 코드에서는 패킷 타입을 의미 있게 표현하기 위해 `PacketType` enum class를 사용합니다.
다만 실제 네트워크 패킷에 들어가는 `PacketHeader::type` 필드는 `int32_t`로 유지합니다.

```cpp
enum class PacketType : int32_t {
    SAFE = -1,
    HEADER_ERROR = 0
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

## 8. 사용 환경

- Windows
- C++
- Winsock2
- Visual Studio

---

## 9. 실행 방법

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

## 10. 향후 계획 요약

단기 목표:

- `SessionID` 도입
- 로그 출력 형식 개선
- `ClientSession`별 `send_mutex` 추가
- `ClientManager::Broadcast()` 구현

중기 목표:

- Broadcast Chat Server 구현
- nickname 기반 클라이언트 식별
- message type 확장
- 송신 실패 세션 정리 정책 구현

장기 목표:

- detach 기반 종료 정책 검증
- send queue 구조 검토
- select() 기반 멀티플렉싱 서버로 확장 가능성 검토
