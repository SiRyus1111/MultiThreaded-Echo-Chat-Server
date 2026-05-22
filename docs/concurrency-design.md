# Concurrency Design

이 문서는 `MultiThreaded Echo Server`의 멀티스레딩, 객체 소유권,
세션 생명주기, 그리고 추후 Broadcast Chat Server 확장을 위한 동기화 설계를 정리합니다.

이 프로젝트에서 가장 중요한 문제는 다음입니다.

```text
멀티스레드 환경에서
각 클라이언트 세션의 수명, 소유권, 송수신 동기화, 종료 상태를
어떻게 안전하게 관리할 것인가?
```

---

## 1. 현재 객체 소유 관계

현재 객체 소유 관계는 다음과 같습니다.

```text
ClientManager
  └── unordered_map<SessionID, shared_ptr<ClientSession>>

client_thread()
  └── shared_ptr<ClientSession>

ClientSession
  ├── unique_ptr<ClientSocket>
  ├── weak_ptr<ClientManager>
  ├── NetState
  ├── atomic<bool> closing
  └── SessionID session_id

ClientSocket
  └── SOCKET
```

이 구조의 핵심은 다음입니다.

```text
ClientManager와 client_thread가 ClientSession을 공유 소유한다.
ClientSession은 ClientSocket을 독점 소유한다.
ClientSession은 ClientManager를 소유하지 않고 weak_ptr로 참조한다.
ClientManager는 SessionID를 key로 ClientSession을 관리한다.
```

`SessionID`는 소유권을 의미하지 않습니다.
`SessionID`는 `ClientManager`가 특정 세션을 찾고 제거하기 위한 서버 내부 식별자입니다.

## 2. shared_ptr의 역할

`ClientSession`은 `ClientManager`뿐만 아니라
각 클라이언트를 담당하는 `client_thread`에서도 사용됩니다.

따라서 `shared_ptr<ClientSession>`으로 세션 객체의 물리적 생존을 관리합니다.

```text
ClientManager가 clients에 보관 중이거나
OR
client_thread가 shared_ptr<ClientSession>을 들고 있으면

ClientSession 객체는 소멸하지 않는다.
```

즉, `shared_ptr`은 다음을 보장합니다.

```text
thread가 ClientSession을 사용하는 동안
ClientSession 객체가 먼저 소멸하지 않도록 한다.

ClientManager의 관리 목록(clients)에 ClientSession이 남아있는 동안
ClientSession 객체가 먼저 소멸하지 않도록 한다.
```

---

## 3. unique_ptr의 역할

`ClientSocket`은 특정 클라이언트 세션 하나에만 속하는 자원입니다.

따라서 `ClientSession`이 `unique_ptr<ClientSocket>`으로 독점 소유합니다.

```text
ClientSession
  └── unique_ptr<ClientSocket>
        └── SOCKET
```

이 구조의 의미는 다음입니다.

```text
특정 클라이언트 socket의 소유자는 해당 ClientSession 하나다.
```

따라서 `ClientSession`이 소멸하면,
소유하고 있던 `ClientSocket`도 함께 소멸하고,
`ClientSocket`의 소멸자에서 raw `SOCKET`이 정리됩니다.

---

## 4. weak_ptr의 역할

`ClientSession`은 자신을 관리하는 `ClientManager`에 접근해야 할 수 있습니다.

예:

- 접속 종료 시 자신을 목록에서 제거 요청
- 추후 채팅 메시지 broadcast 요청
- 서버 공용 상태 접근

하지만 `ClientSession`이 `ClientManager`를 소유해서는 안 됩니다.

따라서 `ClientSession`은 `ClientManager`를 `weak_ptr`로 참조합니다.

```text
ClientManager
  └── shared_ptr<ClientSession>

ClientSession
  └── weak_ptr<ClientManager>
```

이 구조는 순환 참조를 방지합니다.

실제로 `ClientManager`에 접근할 때는 `lock()`을 통해
아직 객체가 살아있는지 확인합니다.

```cpp
if (auto manager = Manager_wp.lock()) {
    manager->RemoveClient(session_id);
}
```

---

## 5. enable_shared_from_this

현재 구조에서 `ClientManager`는 `ClientSession`에게 자기 자신을 `shared_ptr` 형태로 전달해야 합니다.

```cpp
client->AddToManager(shared_from_this());
```

이를 위해 `ClientManager`는 `std::enable_shared_from_this<ClientManager>`를 상속합니다.

```cpp
class ClientManager
    : public std::enable_shared_from_this<ClientManager> {
};
```

주의할 점은 다음입니다.

```text
shared_from_this()를 호출하는 객체는
반드시 이미 shared_ptr로 관리되고 있어야 한다.
```

따라서 `ClientManager`는 다음처럼 생성해야 합니다.

```cpp
auto manager = std::make_shared<ClientManager>();
```

이전 구조에서는 `ClientSession::RemoveThisClient()`가
`shared_from_this()`를 통해 자기 자신의 `shared_ptr`을 `RemoveClient()`에 넘기는 형태였습니다.

하지만 `SessionID` 기반 제거 구조에서는
`ClientSession`이 자기 자신의 `session_id`를 보관하고,
`RemoveClient(session_id)`를 호출하면 됩니다.

```cpp
void ClientSession::RemoveThisClient() {
    if (auto manager = Manager_wp.lock()) {
        manager->RemoveClient(session_id);
    }
}
```

따라서 `RemoveThisClient()`만 놓고 보면
`ClientSession`은 더 이상 `shared_from_this()`에 의존하지 않습니다.

다만 추후 다른 함수에서 `shared_from_this()`를 사용할 가능성이 있다면
`ClientSession`의 `std::enable_shared_from_this<ClientSession>` 상속을 유지할 수 있습니다.
반대로 더 이상 사용할 일이 없다면 장기적으로 제거를 검토할 수 있습니다.

## 6. detach 기반 thread 관리

현재 구조에서는 클라이언트별 thread를 생성한 뒤 `detach()`합니다.

```cpp
std::thread ClientThread(client_thread, session);
ClientThread.detach();
```

`detach()`를 선택한 이유는 현재 단계에서 `join()` 기반으로
thread 종료 시점, `ClientSession` 제거 시점, `ClientManager`의 관리 책임을
모두 정확히 묶는 구조가 지나치게 복잡했기 때문입니다.

현재 구조의 기준은 다음과 같습니다.

```text
std::thread 객체로 client thread의 종료를 직접 회수하지 않는다.
대신 ClientSession의 shared_ptr와 closing 상태값으로
세션 객체의 수명과 종료 상태를 관리한다.
```

중요한 점은 다음입니다.

```text
detach()는 thread를 완전히 방치한다는 의미가 아니라,
std::thread 객체를 통한 회수를 포기하고
다른 수명 / 상태 모델로 관리한다는 의미에 가깝다.
```

---

## 7. closing의 역할

`closing`은 `ClientSession`의 논리적 종료 상태를 나타냅니다.

```cpp
std::atomic<bool> closing = false;
```

의미는 다음과 같습니다.

```text
closing == false
  → 아직 정상 송수신 가능한 세션

closing == true
  → 객체는 아직 살아있지만, 더 이상 정상적인 송신 대상으로 보지 않는 종료 예정 세션
```

`detach()` 기반 구조에서는 `std::thread` 객체를 통해
client thread의 종료 여부를 직접 관측할 수 없습니다.

따라서 분리된 thread의 상태는 `std::thread` 객체가 아니라
`ClientSession` 객체 내부 상태로 기록합니다.

현재는 `Run()` 실행 중 다음 상황이 발생하면 `closing`을 `true`로 바꿉니다.

- transport error
- peer exit
- protocol error

`closing`은 여러 스레드에서 읽고 쓸 수 있으므로 `std::atomic<bool>`을 사용합니다.

단일 bool 값의 원자적인 읽기 / 쓰기만 필요하고,
복잡한 상태 변경을 보호하는 상황은 아니기 때문에
현재 단계에서는 `std::mutex`보다 `std::atomic`이 더 적절하다고 판단합니다.

---

## 8. RemoveClient의 의미

`RemoveClient()`는 세션 객체를 즉시 소멸시키는 함수가 아닙니다.

SessionID 도입 이후 `RemoveClient()`는 다음 형태가 됩니다.

```cpp
void RemoveClient(SessionID id);
```

의미는 다음과 같습니다.

```text
RemoveClient(SessionID id)
  → ClientManager의 clients unordered_map에서 해당 key 제거
  → ClientManager의 관리 목록에서 제외
  → 마지막 shared_ptr이 사라진 경우에만 ClientSession 소멸
```

따라서 다음을 구분해야 합니다.

```text
shared_ptr
  → 객체의 물리적 생존 보장

closing
  → 세션의 논리적 종료 상태

SessionID
  → Manager가 세션을 식별하기 위한 서버 내부 ID

RemoveClient()
  → ClientManager의 관리 목록에서 제거
```

이 구분은 현재 설계에서 가장 중요한 기준입니다.

객체가 아직 살아있다는 것과,
그 객체가 정상적인 송수신 대상으로 남아있다는 것은 서로 다른 문제입니다.

또한 `SessionID`는 객체 생존을 보장하지 않습니다.
세션의 생존은 여전히 `shared_ptr`이 담당하고,
`SessionID`는 `ClientManager`가 어떤 세션을 제거할지 식별하는 기준으로만 사용됩니다.

## 9. 종료 흐름

전체 종료 흐름은 다음과 같습니다.

```text
1. client_thread가 transport error / peer exit / protocol error를 감지한다.
2. ClientSession::closing을 true로 변경한다.
3. 이후 ClientManager의 broadcast는 해당 세션을 send 대상에서 제외한다.
4. client_thread는 Run() 종료 전에 RemoveThisClient()를 호출한다.
5. RemoveThisClient()는 Manager_wp.lock()으로 ClientManager에 접근한다.
6. RemoveThisClient()는 자기 자신의 session_id를 넘겨 RemoveClient(session_id)를 호출한다.
7. ClientManager::RemoveClient(SessionID)가 clients unordered_map에서 해당 key를 제거한다.
8. Run()이 return되면 client_thread가 들고 있던 shared_ptr도 해제된다.
9. 마지막 shared_ptr이 사라지면 ClientSession이 소멸한다.
10. ClientSession이 소유하던 ClientSocket도 소멸하면서 raw SOCKET이 closesocket()으로 정리된다.
```

이 흐름에서 중요한 점은 `closing = true`가 객체 소멸을 의미하지 않는다는 것입니다.
`closing`은 논리적 종료 상태이며, 실제 객체 소멸은 마지막 `shared_ptr`이 사라지는 시점에 발생합니다.

또한 `RemoveClient(session_id)`는 key 기반으로 `ClientManager`의 관리 목록에서 세션을 제거하는 동작입니다.
이 함수가 호출되었다고 해서 즉시 `ClientSession` 객체가 소멸한다고 보장할 수는 없습니다.

## 10. ClientManager lock

`ClientManager`는 접속 중인 `ClientSession` 목록을 관리합니다.

```cpp
std::unordered_map<SessionID, std::shared_ptr<ClientSession>> clients;
std::mutex clients_mutex;
```

`clients`는 여러 thread가 접근할 수 있는 공유 컨테이너입니다.

따라서 다음 작업은 mutex로 보호해야 합니다.

- 새 client 추가
- 기존 client 제거
- client 목록 snapshot 생성
- 추후 특정 SessionID 기반 조회

예상 구조:

```cpp
void ClientManager::AddClient(std::shared_ptr<ClientSession> client, SessionID id) {
    client->AddToManager(shared_from_this());

    std::lock_guard<std::mutex> lock(clients_mutex);
    clients[id] = client;
}
```

```cpp
void ClientManager::RemoveClient(SessionID id) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    clients.erase(id);
}
```

현재 `SessionID` 부여는 `main thread`의 accept loop에서만 수행합니다.
따라서 단일 accept thread 구조에서는 ID 증가 변수 자체에 별도 lock이 필요하지 않습니다.

하지만 `clients` 컨테이너는 여러 client thread에서 제거 요청이 들어올 수 있으므로,
`AddClient()` / `RemoveClient()` / snapshot 생성은 `clients_mutex`로 보호합니다.

추후 중복 ID 삽입을 더 엄격히 검사하고 싶다면
`clients[id] = client` 대신 `clients.emplace(id, client)`를 사용하고
삽입 성공 여부를 확인하는 구조도 검토할 수 있습니다.

## 11. Broadcast 전체 lock 방식의 문제

처음 생각할 수 있는 방식은 `Broadcast()` 전체에 하나의 lock을 거는 것입니다.

```text
Broadcast() 시작
  → clients lock
  → 모든 ClientSession에 send
  → clients unlock
```

하지만 이 방식은 문제가 있습니다.

- 한 클라이언트에게 `send()` 중일 때 다른 클라이언트 관련 작업까지 막힘
- 느린 클라이언트 하나가 전체 broadcast를 지연시킬 수 있음
- `send()`처럼 block될 수 있는 작업을 manager lock 안에서 수행하게 됨
- 결과적으로 전체 병렬성이 낮아짐

따라서 `clients_mutex`를 잡은 상태에서 실제 `send()`를 수행하지 않는 구조가 필요합니다.

---

## 12. ClientManager lock과 ClientSession send lock 분리

현재 설계에서는 lock의 역할을 다음처럼 분리할 예정입니다.

```text
ClientManager의 clients_mutex
  → 현재 접속 중인 ClientSession map 보호

ClientSession의 send_mutex
  → 해당 클라이언트에게 보내는 Header + Payload의 패킷 경계 보호
```

핵심은 다음입니다.

```text
보호해야 하는 것은 “브로드캐스트 전체”가 아니라,
“같은 ClientSession에 대한 패킷 송신 순서”이다.
```

26.05.20(수) 리팩토링으로 `ClientSession::SendPacket()`이 1차 구현되었습니다.
따라서 추후 `send_mutex`는 새 송신 함수를 따로 만드는 방식이 아니라,
이미 존재하는 `SendPacket()` 내부의 Header + Payload 송신 구간을 감싸는 방식으로 적용할 수 있습니다.

같은 클라이언트에게 다음과 같이 패킷이 섞이면 안 됩니다.

```text
잘못된 예:

Header_A
Header_B
Payload_A
Payload_B
```

원하는 송신 순서는 다음과 같습니다.

```text
올바른 예:

Header_A
Payload_A
Header_B
Payload_B
```

예상 구조:

```cpp
class ClientSession {
private:
    std::mutex send_mutex;

public:
    NetState SendPacket(const char* msg, uint32_t len, PacketType type) {
        std::lock_guard<std::mutex> lock(send_mutex);

        // 1. PacketHeader 구성
        // 2. Header 송신
        // 3. Payload 송신
    }
};
```

---

## 13. Broadcast snapshot 구조

`ClientManager::Broadcast()`는 다음 흐름으로 구현할 예정입니다.

```text
1. ClientManager가 clients_mutex를 잡는다.
2. 현재 clients 컨테이너의 snapshot을 복사한다.
3. clients_mutex를 해제한다.
4. snapshot을 순회한다.
5. closing == true인 세션은 건너뛴다.
6. 각 ClientSession의 SendPacket()을 호출한다.
7. SendPacket() 내부에서는 해당 ClientSession의 send_mutex만 잡는다.
```

예상 구조:

```cpp
void ClientManager::Broadcast(
    const char* msg,
    uint32_t len,
    PacketType type
) {
    std::vector<std::shared_ptr<ClientSession>> snapshot;

    {
        std::lock_guard<std::mutex> lock(clients_mutex);

        for (auto& client : clients) {
            snapshot.push_back(client);
        }
    }

    for (auto& client : snapshot) {
        if (client->IsClosing()) {
            continue;
        }

        client->SendPacket(msg, len, type);
    }
}
```

이 구조의 장점은 다음과 같습니다.

- `clients_mutex`는 clients 목록을 복사하는 짧은 구간에서만 사용
- 실제 `send()`는 `clients_mutex`를 잡지 않은 상태에서 수행
- 같은 `ClientSession`에 대한 송신만 `send_mutex`로 직렬화
- 서로 다른 `ClientSession`에 대한 send lock이 서로를 직접 막지 않음
- Header / Payload 패킷 경계가 섞이는 문제를 방지할 수 있음

다만 하나의 `Broadcast()` 호출 내부에서 `snapshot`을 단일 thread가 순회한다면,
그 `Broadcast()` 내부의 send 호출 자체는 순차적으로 진행됩니다.

즉, 현재 설계의 의미는 다음입니다.

```text
모든 송신이 완전히 병렬로 수행된다는 뜻이 아니라,
서로 다른 ClientSession에 대한 send lock이 서로를 직접 막지 않도록
락 범위를 세분화한다는 뜻이다.
```

---

## 14. 아직 남은 정책 결정

Broadcast 단계에서 아직 결정해야 할 정책은 다음과 같습니다.

- sender 자신에게도 메시지를 보낼 것인가?
- `SendPacket()` 실패 시 즉시 `closing = true`로 바꿀 것인가?
- 송신 실패한 세션을 Broadcast 내부에서 제거할 것인가?
- 제거 요청은 즉시 수행할 것인가, 별도 정리 단계에서 수행할 것인가?
- 느린 클라이언트가 있을 때 send timeout 또는 send queue를 둘 것인가?
- detach 기반 구조를 계속 유지할 것인가, thread 관리 모델을 바꿀 것인가?

현재 단계에서는 Echo Server 구조 안정화가 우선입니다.

---

## 15. 향후 send queue 구조 검토

현재 설계는 `Broadcast()`가 각 `ClientSession::SendPacket()`을 직접 호출하는 구조입니다.

하지만 클라이언트 수가 많아지거나 느린 클라이언트 문제가 커지면,
추후 다음 구조를 검토할 수 있습니다.

```text
ClientSession
  ├── send_queue
  ├── send_mutex
  ├── condition_variable
  └── dedicated sender thread 또는 event loop
```

다만 현재 단계에서는 구조가 지나치게 복잡해질 수 있으므로,
우선은 다음 구조를 목표로 합니다.

```text
clients snapshot
  + per-session send_mutex
  + closing check
```

---

## 16. 정리

현재 멀티스레딩 설계의 핵심은 다음과 같습니다.

```text
shared_ptr
  → ClientSession의 물리적 생존 보장

closing
  → ClientSession의 논리적 종료 상태

RemoveClient()
  → ClientManager의 관리 목록에서 제거

clients_mutex
  → ClientManager의 clients 컨테이너 보호

send_mutex
  → 특정 ClientSession에 대한 Header + Payload 송신 순서 보호
```

그리고 가장 중요한 원칙은 다음입니다.

```text
manager lock을 잡은 상태에서 blocking될 수 있는 send()를 오래 수행하지 않는다.
```
