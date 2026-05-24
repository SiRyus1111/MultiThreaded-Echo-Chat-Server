# Architecture

이 문서는 `MultiThreaded Echo Server`의 전체 구조와 책임 분리를 정리합니다.

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

## 2. 전체 실행 구조

현재 서버의 실행 구조는 다음과 같습니다.

```text
main thread
  ├── WinsockGuard 생성
  ├── ListenSocket 생성
  ├── LineLogger::GetInstance() 기반 콘솔 출력 사용
  ├── SessionID의 초기값 준비
  ├── accept() loop
  │     ├── ClientSocket 생성
  │     ├── 현재 연결에 SessionID 부여
  │     ├── ClientSession 생성
  │     ├── ClientManager::AddClient(session, session_id)
  │     ├── 다음 SessionID 설정
  │     ├── std::thread ClientThread(client_thread, session)
  │     └── ClientThread.detach()
  └── accept() loop 계속 진행
```

핵심은 `main thread`가 각 클라이언트의 송수신을 직접 처리하지 않는다는 점입니다.

`main thread`는 새 연결을 받아 `SessionID`를 부여하고,
해당 연결을 `ClientSession`으로 묶은 뒤,
그 세션을 담당할 `client_thread`를 시작하는 역할에 집중합니다.

현재 구조에서는 `SessionID` 부여가 `main thread`의 accept loop 안에서만 일어납니다.
따라서 여러 클라이언트가 동시에 접속하더라도,
세션 ID를 증가시키는 변수에 대한 레이스 컨디션을 걱정할 필요가 없습니다.

```cpp
using SessionID = uint64_t;

SessionID next_session_id = INITIAL_SESSION_ID;

while (true) {
    // accept() 성공 후
    SessionID session_id = next_session_id++;

    auto session = std::make_shared<ClientSession>(
        std::move(client_socket),
        client_addr,
        session_id
    );

    manager->AddClient(session, session_id);
}
```

단, 추후 여러 thread가 accept를 수행하는 구조로 확장한다면
`next_session_id`는 `std::atomic<SessionID>` 또는 mutex로 보호해야 합니다.

---

## 3. main thread

`main thread`는 서버의 진입점이며,
새로운 클라이언트 연결을 받아들이는 역할을 담당합니다.

### 역할

- `WinsockGuard` 생성
- `ListenSocket` 생성
- `LineLogger` 기반 서버 전역 로그 출력
- `accept()` loop 실행
- 새 클라이언트 연결 수락
- `ClientSocket` 생성
- 새 연결에 `SessionID` 부여
- `ClientSession` 생성
- `ClientManager`에 `SessionID`와 함께 세션 등록
- 클라이언트별 `std::thread` 생성
- 생성한 thread를 `detach()`

`main thread`는 다음 일에 집중합니다.

```text
새 클라이언트 연결을 받아
SessionID를 부여하고
ClientSession 객체로 만든 뒤
client_thread에 넘긴다.
```

각 클라이언트의 실제 송수신 흐름은 `ClientSession::Run()`에서 처리합니다.

---

## 4. client_thread

`client_thread`는 특정 클라이언트 하나를 담당하는 실행 흐름입니다.

현재 형태는 다음과 같습니다.

```cpp
void client_thread(std::shared_ptr<ClientSession> session) {
    session->Run();
}
```

### 역할

- 특정 `ClientSession` 하나를 인자로 받음
- `ClientSession::Run()` 실행
- `Run()` 내부에서 `RecvPacket()` / `SendPacket()`을 호출하여 Echo 흐름 수행
- 해당 클라이언트의 송 / 수신 루프 수행
- transport error / peer exit / protocol error 감지
- 종료 상황 발생 시 `closing = true` 설정
- 종료 후 `TransportExceptionHandling()`을 통해 상태별 후처리 수행
- 종료 전 `ClientManager`에 자기 세션 제거 요청

현재 Echo Server 단계에서는 수신한 메시지를 같은 클라이언트에게 다시 돌려보냅니다.

```text
Client A → Server → Client A
```

현재 `Run()`은 Echo Server의 고수준 흐름만 담당합니다.

```text
Run()
  ├── RecvPacket()
  ├── 수신 상태 확인
  ├── SendPacket()
  ├── 송신 상태 확인
  └── transport error / peer exit / protocol error 감지
        ├── MarkClosing()
        └── TransportExceptionHandling()
```

추후 Chat Server 단계에서는 수신한 메시지를 그대로 echo하지 않고,
`ClientManager::Broadcast()`로 전달하는 구조로 확장할 예정입니다.

```text
Client A → Server → Client B
                  → Client C
                  → Client D
```

---

## 5. ClientSession

`ClientSession`은 클라이언트 한 명의 연결 단위를 표현하는 객체입니다.

즉, 클라이언트 한 명과 관련된 socket, 주소, 세션 ID, 통신 상태, 종료 상태,
패킷 송수신 흐름을 하나의 객체로 묶습니다.

### 역할

- 클라이언트 한 명의 연결 단위 표현
- 해당 클라이언트의 `ClientSocket` 소유
- 클라이언트 주소 정보 저장
- 로그 출력을 위한 클라이언트 IP 문자열 `ClientAddrStr` 저장
- 서버 내부 세션 식별자인 `SessionID` 저장
- 클라이언트별 `NetState` 저장
- 클라이언트별 논리적 종료 상태 `closing` 관리
- Echo Server 단계의 고수준 실행 흐름 `Run()` 구현
- `RecvPacket()`으로 패킷 수신, 수신 후 상태(`NetState`) 반환
- `SendPacket()`으로 패킷 송신, 송신 후 상태(`NetState`) 반환
- `TransportExceptionHandling()`으로 통신 종료 / 예외 후처리
- 종료 시 `ClientManager`에 자기 `SessionID` 기반 제거 요청
- 추후 nickname / 접속 상태 관리

현재 구조는 다음과 같습니다.

```cpp
class ClientSession : public std::enable_shared_from_this<ClientSession> {
private:
    std::unique_ptr<ClientSocket> ClientSock;
    sockaddr_in ClientAddr;
    char ClientAddrStr[INET_ADDRSTRLEN];
    std::weak_ptr<ClientManager> Manager_wp;
    NetState ClientState;
    std::atomic<bool> closing = false;
    SessionID session_id;

public:
    ClientSession(std::unique_ptr<ClientSocket> s, sockaddr_in addr, SessionID id);

    void AddToManager(std::shared_ptr<ClientManager> manager_sp);
    void Run();

    NetState RecvPacket(char* buf);
    NetState SendPacket(const char* msg, uint32_t len, PacketType type);

    void MarkClosing();
    void TransportExceptionHandling();
    void RemoveThisClient();
};
```

### 설계 관점

`ClientSession`은 단순한 데이터 묶음이 아닙니다.

```text
ClientSession
  → 클라이언트 한 명의 생명주기와 통신 흐름을 표현하는 객체
```

따라서 ClientSocket, ClientAddr, ClientAddrStr, SessionID, NetState, closing은
모두 세션 내부에 위치하는 것이 자연스럽습니다.

`SessionID`는 `ClientManager`가 세션을 빠르게 식별하기 위한 서버 내부 ID입니다.
IP:Port만으로는 세션 관리가 애매할 수 있고,
`shared_ptr<ClientSession>` 대조 기반 제거는 컨테이너 구조가 커질수록 불편해질 수 있습니다.

따라서 현재 구조에서는 `ClientSession`이 자기 자신의 `session_id`를 값으로 보관하고,
종료 시 `ClientManager::RemoveClient(session_id)`를 호출하도록 설계합니다.

그리고 26.05.20(수) 리팩토링으로 `Run()`은 더 이상 Header / Payload 송수신 세부 절차를 직접 담당하지 않습니다.
`Run()`은 Echo Server의 실행 흐름을 제어하고, 실제 패킷 송수신은 `RecvPacket()` / `SendPacket()`이 담당합니다.

```text
Run()
  → Echo Server 흐름 제어

RecvPacket()
  → PacketHeader + Payload 수신

SendPacket()
  → PacketHeader + Payload 송신

TransportExceptionHandling()
  → 오류 / 종료 상태별 후처리
```

따라서 나중에 Chat Server로 확장할 때는 `Run()`의 메시지 처리 부분을 `Broadcast()` 호출로 바꾸더라도,
패킷 송수신 절차는 그대로 재사용할 수 있습니다.

## 6. ClientManager

`ClientManager`는 현재 접속 중인 여러 `ClientSession`을 관리하는 객체입니다.

### 역할

- 새 `ClientSession` 추가
- 종료된 `ClientSession` 제거
- 현재 접속 중인 클라이언트 목록 관리
- `SessionID` 기반 세션 식별
- clients 컨테이너 동기화
- 추후 broadcast 수행

현재 구조는 다음과 같습니다.

```cpp
class ClientManager : public std::enable_shared_from_this<ClientManager> {
private:
    std::unordered_map<SessionID, std::shared_ptr<ClientSession>> clients;
    std::mutex clients_mutex;

public:
    void AddClient(std::shared_ptr<ClientSession> client, SessionID id);
    void RemoveClient(SessionID id);
};
```

`ClientManager`는 개별 클라이언트의 세부 송수신 과정을 직접 처리하지 않습니다.

대신 여러 `ClientSession` 사이의 관계와 목록을 관리합니다.

```text
ClientManager
  → 여러 ClientSession의 등록, 제거, 순회, broadcast를 담당
```

이때 세션 식별 기준은 `shared_ptr<ClientSession>` 대조가 아니라 `SessionID`입니다.

```text
key   = SessionID
value = shared_ptr<ClientSession>
```

따라서 특정 세션을 제거할 때는 `clients.erase(session_id)`처럼
key 기반으로, O(N)의 시간복잡도로 관리 목록에서 제거할 수 있습니다.

## 7. 객체 책임 요약

| 구성 요소 | 책임 |
|---|---|
| `main thread` | 새 연결 수락, 세션 생성, client thread 시작 |
| `client_thread` | 특정 세션 하나의 실행 흐름 담당 |
| `ClientSession` | 클라이언트 한 명의 socket, SessionID, 상태, 송수신 흐름 관리 |
| `ClientManager` | SessionID 기반으로 여러 세션의 목록과 관계 관리 |
| `ClientSocket` | raw `SOCKET` 소유 및 송수신 |
| `NetState` | 송수신 단계와 예외 상태 기록 |

---

## 8. Echo Server에서 Chat Server로의 확장

현재 Echo Server 단계에서는 각 클라이언트가 보낸 메시지를
해당 클라이언트에게 다시 돌려보냅니다.

```text
Client A → Server → Client A
```

추후 Chat Server 단계에서는 `ClientManager::Broadcast()`를 추가하여,
한 클라이언트가 보낸 메시지를 다른 클라이언트들에게 전달합니다.

```text
Client A → Server → Client B
                  → Client C
                  → Client D
```

이때 중요한 설계 문제는 다음과 같습니다.

- 현재 접속 중인 `ClientSession` 목록을 안전하게 순회하는 방법
- `closing == true`인 세션을 송신 대상에서 제외하는 방법
- sender 자신에게도 메시지를 보낼지 여부
- 같은 클라이언트에 대한 Header / Payload 송신 순서 보장
- `send()` 중 block될 수 있는 구간과 manager lock 범위 분리
- 송신 실패한 클라이언트 정리 정책

이 내용은 [`concurrency-design.md`](concurrency-design.md)에서 더 자세히 다룹니다.

---

## 9. LineLogger와 콘솔 출력 책임

`LineLogger`는 서버의 콘솔 출력 경로를 단일화하기 위한 공용 로깅 컴포넌트입니다.

멀티스레드 서버에서는 `main thread`, 여러 `client_thread`, 추후 `ClientManager::Broadcast()` 흐름 등
여러 실행 흐름이 동시에 로그를 출력할 수 있습니다.
따라서 콘솔 출력은 하나의 공유 자원인 `std::cout`에 대한 동기화 정책을 가져야 합니다.

`LineLogger`는 다음 책임을 가집니다.

- 프로젝트의 콘솔 출력 경로 단일화
- 전역 단일 `LineLogger` 객체 제공
- 하나의 `output_mutex_`로 `std::cout` 보호
- 로그 문자열을 먼저 조립한 뒤 출력
- 완성된 로그 문자열 하나를 한 번의 `operator<<`로 출력
- `SessionID` / `IP:Port` / `LogType` 기반 세션 로그 형식 제공
- `LogType` enum class를 통한 로그 타입 제한

구조적으로는 다음과 같습니다.

```text
main thread / client_thread / ClientManager
  → LineLogger::GetInstance()
  → WriteLog() 또는 WriteSessionLog()
  → 하나의 output_mutex_로 std::cout 보호
```

세션 로그의 기본 형식은 다음을 목표로 합니다.

```text
[SessionID 3][127.0.0.1:53021][RECV_COMPLETE] payload received
```

현재 `LineLogger` 라이브러리는 구현 완료 상태이며,
프로젝트 전체의 기존 `std::cout` 출력은 이후 `LineLogger` 기반으로 교체할 예정입니다.

---

## 10. 설계 핵심

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
