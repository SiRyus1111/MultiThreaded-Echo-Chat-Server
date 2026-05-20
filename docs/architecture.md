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
  ├── accept() loop
  │     ├── ClientSocket 생성
  │     ├── ClientSession 생성
  │     ├── ClientManager::AddClient(session)
  │     ├── std::thread ClientThread(client_thread, session)
  │     └── ClientThread.detach()
  └── accept() loop 계속 진행
```

핵심은 `main thread`가 각 클라이언트의 송수신을 직접 처리하지 않는다는 점입니다.

`main thread`는 새 연결을 받아 `ClientSession`으로 묶고,
해당 세션을 담당할 `client_thread`를 시작하는 역할에 집중합니다.

---

## 3. main thread

`main thread`는 서버의 진입점이며,
새로운 클라이언트 연결을 받아들이는 역할을 담당합니다.

### 역할

- `WinsockGuard` 생성
- `ListenSocket` 생성
- `accept()` loop 실행
- 새 클라이언트 연결 수락
- `ClientSocket` 생성
- `ClientSession` 생성
- `ClientManager`에 세션 등록
- 클라이언트별 `std::thread` 생성
- 생성한 thread를 `detach()`

`main thread`는 다음 일에 집중합니다.

```text
새 클라이언트 연결을 받아
ClientSession 객체로 만들고
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

즉, 클라이언트 한 명과 관련된 socket, 주소, 통신 상태, 종료 상태, 패킷 송수신 흐름을 하나의 객체로 묶습니다.

### 역할

- 클라이언트 한 명의 연결 단위 표현
- 해당 클라이언트의 `ClientSocket` 소유
- 클라이언트 주소 정보 저장
- 로그 출력을 위한 클라이언트 IP 문자열 `ClientAddrStr` 저장
- 클라이언트별 `NetState` 저장
- 클라이언트별 논리적 종료 상태 `closing` 관리
- Echo Server 단계의 고수준 실행 흐름 `Run()` 구현
- `RecvPacket()`으로 패킷 수신, 수신 후 상태(`NetState`) 반환
- `SendPacket()`으로 패킷 송신, 송신 후 상태(`NetState`) 반환
- `TransportExceptionHandling()`으로 통신 종료 / 예외 후처리
- 종료 시 `ClientManager`에 자기 자신 제거 요청
- 추후 nickname / `SessionID` / 접속 상태 관리

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

public:
    ClientSession(std::unique_ptr<ClientSocket> s, sockaddr_in addr);

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

따라서 ClientSocket, NetState, closing은 모두 세션 내부에 위치하는 것이 자연스럽습니다.

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
- clients 컨테이너 동기화
- 추후 broadcast 수행

현재 구조는 다음과 같습니다.

```cpp
class ClientManager : public std::enable_shared_from_this<ClientManager> {
private:
    std::vector<std::shared_ptr<ClientSession>> clients;
    std::mutex client_mutex;

public:
    void AddClient(std::shared_ptr<ClientSession> client);
    void RemoveClient(std::shared_ptr<ClientSession> client);
};
```

`ClientManager`는 개별 클라이언트의 세부 송수신 과정을 직접 처리하지 않습니다.

대신 여러 `ClientSession` 사이의 관계와 목록을 관리합니다.

```text
ClientManager
  → 여러 ClientSession의 등록, 제거, 순회, broadcast를 담당
```

---

## 7. 객체 책임 요약

| 구성 요소 | 책임 |
|---|---|
| `main thread` | 새 연결 수락, 세션 생성, client thread 시작 |
| `client_thread` | 특정 세션 하나의 실행 흐름 담당 |
| `ClientSession` | 클라이언트 한 명의 socket, 상태, 송수신 흐름 관리 |
| `ClientManager` | 여러 세션의 목록과 관계 관리 |
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

## 9. 설계 핵심

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
