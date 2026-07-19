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
- `HEADER_ERROR` 타입의 패킷 수신 (`HandleRecvPacket()` 내부에서 `TryMarkClosing()` 성공 시)

마지막 항목은 수신 자체의 실패가 아니라, **수신된 패킷의 내용에 의한 종료**입니다.
하지만 "이 세션은 더 이상 정상적인 송수신 대상이 아니다"라는 `closing`의 의미에 부합하므로,
동일한 플래그와 `TryMarkClosing()` 함수를 통해 표현합니다.

즉, `closing = true`의 기준은 **"해당 세션을 종료해야 하는 상황인가"** 로 일관되게 유지됩니다.
트리거가 transport 계층의 실패이든, 수신된 패킷의 의미이든 관계없이 동일한 기준을 적용합니다.

`closing`은 여러 스레드에서 읽고 쓸 수 있으므로 `std::atomic<bool>`을 사용합니다.

단일 bool 값의 원자적인 읽기 / 쓰기만 필요하고,
복잡한 상태 변경을 보호하는 상황은 아니기 때문에
현재 단계에서는 `std::mutex`보다 `std::atomic`이 더 적절하다고 판단합니다.

---

## 8. TryMarkClosing()의 원자성과 RemoveThisClient() 페어링

`closing`을 `true`로 바꾸는 시점은 단순히 상태 하나를 갱신하는 것이 아니라,
"이 세션의 종료 후처리(로그 출력, `RemoveThisClient()` 호출)를 누가 담당할 것인가"를 정하는 시점이기도 합니다.

기존 `MarkClosing()`은 다음과 같이 무조건 `closing`을 `true`로 바꾸기만 하는 함수였습니다.

```cpp
void MarkClosing() {
    closing.store(true);
}
```

이 방식은 다음 두 상황에서 같은 세션에 대해 종료 후처리가 중복 실행되는 것을 막지 못했습니다.

```text
1. 기존에도 있던 문제
   HandleTransportException()의 protocol_error 분기는
   에러 패킷 송신에 실패하면 자기 자신을 재귀 호출한다.
   이 재귀 호출로 인해 로그 출력과 RemoveThisClient()가 두 번 실행될 수 있었다.

2. 향후 Broadcast 단계에서 예상되는 문제
   Broadcast()가 도입되면 다른 client_thread가
   같은 ClientSession의 SendPacket()을 호출하다 실패하여
   HandleTransportException()을 동시에 트리거할 수 있다.
```

이를 막기 위해 `MarkClosing()`을 `TryMarkClosing()`으로 개편했습니다.
`std::atomic<bool>::compare_exchange_strong()`을 사용해
`closing`을 `false → true`로 바꾸는 시도가 **정확히 한 번만 성공**하도록 만듭니다.

```cpp
bool TryMarkClosing() {
    bool expected = false;

    if (!closing.compare_exchange_strong(expected, true)) {
        return false;
    }

    return true;
}
```

```text
TryMarkClosing()
  → 성공(true 반환): 이 호출이 최초로 종료를 감지함, 이후 종료 후처리 책임을 가짐
  → 실패(false 반환): 이미 다른 호출/스레드가 종료 처리 중, 아무것도 하지 않고 즉시 반환
```

`std::mutex::try_lock()`과 비슷한 감각입니다.
`lock()`처럼 실패 시 대기하지 않고, 실패하면 즉시 포기합니다.

`TryMarkClosing()`과 `RemoveThisClient()`는 다음처럼 항상 세트로 취급합니다.

```text
TryMarkClosing()을 호출해 성공한 쪽만
RemoveThisClient()를 호출할 책임을 가진다.

TryMarkClosing()에 실패한 쪽은
RemoveThisClient()를 호출하지 않는다.
```

이 페어링을 강제하기 위해 `TryMarkClosing()` 호출 위치를 `Run()`에서
`HandleTransportException()` 내부 최상단으로 옮겼습니다.
`HEADER_ERROR` 타입 패킷처럼 `HandleTransportException()`을 거치지 않는 경로(`HandleRecvPacket()`)에도
동일하게 `TryMarkClosing()` 성공 시에만 `RemoveThisClient()`를 호출하는 패턴을 적용합니다.

자세한 함수 설계는 [server-component-design.md §5-6](server-component-design.md#5-6-trymarkclosing)을 참고합니다.

---

## 9. RemoveClient의 의미

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

## 10. 종료 흐름

전체 종료 흐름은 다음과 같습니다.

```text
종료 흐름은 두 경로로 발생합니다.

[경로 A] 수신 과정 자체에서 예외 발생 (transport error / peer exit / protocol error)
1. RecvPacket()이 RecvResult.state에 예외 상태를 기록하여 반환한다.
2. Run()이 state를 확인하고 HandleTransportException(state)를 호출한다.
3. HandleTransportException() 내부 최상단에서 TryMarkClosing()을 시도한다.
   - 실패하면 이미 다른 경로에서 종료 처리 중이므로 즉시 반환한다.
   - 성공하면 상태별 로그 / 에러 응답 처리 후, 함수 마지막에 RemoveThisClient()를 호출한다.
4. Run()이 break한다.

[경로 B] 정상 수신된 패킷의 내용에 의한 종료 (HEADER_ERROR 수신 등)
1. RecvPacket()이 type = HEADER_ERROR인 RecvResult를 반환한다.
2. Run()이 HandleRecvPacket(res)를 호출한다.
3. HandleRecvPacket() 내부에서 TryMarkClosing()을 시도하고, 성공한 경우에만 RemoveThisClient()를 호출한다.
4. Run()이 HandleRecvPacket() 이후 closing 상태를 확인하고 break한다.

[공통 경로]
5. 이후 ClientManager의 broadcast는 해당 세션을 send 대상에서 제외한다.
6. RemoveThisClient()는 Manager_wp.lock()으로 ClientManager에 접근한다.
7. RemoveThisClient()는 자기 자신의 session_id를 넘겨 RemoveClient(session_id)를 호출한다.
8. ClientManager::RemoveClient(SessionID)가 clients unordered_map에서 해당 key를 제거한다.
9. Run()이 return되면 client_thread가 들고 있던 shared_ptr도 해제된다.
10. 마지막 shared_ptr이 사라지면 ClientSession이 소멸한다.
11. ClientSession이 소유하던 ClientSocket도 소멸하면서 raw SOCKET이 closesocket()으로 정리된다.
```

이 흐름에서 중요한 점은 `closing = true`가 객체 소멸을 의미하지 않는다는 것입니다.
`closing`은 논리적 종료 상태이며, 실제 객체 소멸은 마지막 `shared_ptr`이 사라지는 시점에 발생합니다.

또한 `RemoveClient(session_id)`는 key 기반으로 `ClientManager`의 관리 목록에서 세션을 제거하는 동작입니다.
이 함수가 호출되었다고 해서 즉시 `ClientSession` 객체가 소멸한다고 보장할 수는 없습니다.

## 11. ClientManager lock

`ClientManager`는 접속 중인 `ClientSession` 목록을 관리합니다.

```cpp
std::unordered_map<SessionID, std::shared_ptr<ClientSession>> clients;
std::mutex clients_mutex;
```

`clients`는 여러 thread가 접근할 수 있는 공유 컨테이너입니다.

따라서 다음 작업은 mutex로 보호해야 합니다.

따라서 다음 작업은 mutex로 보호해야 합니다.

- 새 client 추가
- 기존 client 제거
- client 목록 snapshot 생성 (`GetClients()`)
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

`GetClients()`는 `clients` snapshot을 반환하는 함수입니다.

```cpp
std::unordered_map<SessionID, std::shared_ptr<ClientSession>> ClientManager::GetClients() {
    std::lock_guard<std::mutex> lock(clients_mutex);
    return clients;
}
```

이 함수를 사용하는 주요 시나리오는 닉네임 중복 검사, 추후의 브로드캐스트입니다.

```text
닉네임 중복 검사 흐름:

1. ClientSession이 Manager_wp.lock()으로 ClientManager에 접근
2. GetClients()를 호출하여 clients snapshot 획득 (clients_mutex 내부에서 복사 후 lock 해제)
3. snapshot을 순회하며 res.payload(설정하려는 닉네임)와 기존 세션의 nickname 비교
4. 중복 발견 시 NICKNAME_CHANGE_FAILED 송신; 없으면 닉네임 갱신 후 NICKNAME_CHANGE_SUCESS 송신
```

이 구조의 핵심은 `clients_mutex`를 잡은 상태에서 snapshot만 복사하고 즉시 lock을 해제한 뒤,
lock 없이 snapshot을 순회하는 점입니다.
`send()`처럼 block될 수 있는 작업을 `clients_mutex` 보호 구간 안에서 수행하지 않습니다.

중복 검사 시 비교 대상은 `res.nick`(송신자의 현재 헤더 닉네임)이 아닌
`res.payload`(설정하려는 새 닉네임)임에 유의합니다.
`res.nick`으로 비교하면 두 번째 이후 닉네임 변경이 항상 실패합니다.

## 12. Broadcast 전체 lock 방식의 문제

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

## 13. ClientManager lock과 ClientSession send lock 분리

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

## 14. Broadcast snapshot 구조

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

## 15. 로그 출력 동기화

멀티스레드 환경에서는 여러 스레드가 동시에 `std::cout`을 사용할 수 있습니다.

기존 방식은 하나의 로그를 여러 번의 `operator<<()` 호출로 출력합니다.

```cpp
std::cout << a
          << b
          << c
          << '\n';
```

이 경우 각 출력 조각 사이에 다른 스레드의 출력이 끼어들 수 있습니다.
따라서 로그 한 줄이 하나의 의미 단위로 보존되지 않을 수 있습니다.

이를 줄이기 위해 프로젝트는 `LineLogger`를 사용합니다.

---

### 15-1. std::cout 보호 mutex 단일화

`std::cout`은 프로젝트 전체에서 공유되는 출력 자원입니다.

따라서 모든 콘솔 출력은 동일한 mutex로 보호되어야 합니다.
만약 `LineLogger` 객체가 여러 개 생성되면,
각 객체가 서로 다른 mutex를 가지게 됩니다.
그러면 같은 `std::cout`을 서로 다른 mutex로 보호하는 문제가 생길 수 있습니다.

이를 막기 위해 `LineLogger`는 싱글톤 패턴을 사용합니다.

```cpp
LineLogger::GetInstance().WriteLog("server started");
```

이 구조의 의미는 다음과 같습니다.

```text
모든 콘솔 출력
  → LineLogger::GetInstance()
  → 동일한 LineLogger 객체
  → 동일한 output_mutex_
  → 동일한 std::cout 보호 정책
```

---

### 15-2. 문자열 조립과 출력 구간 분리

`LineLogger`는 먼저 `std::ostringstream`로 로그 문자열을 완성합니다.

```cpp
std::ostringstream oss;
(oss << ... << std::forward<Args>(args));
oss << '\n';
```

이 문자열 조립 과정은 각 호출의 지역 객체에서 수행되므로,
다른 스레드와 공유되는 자원을 직접 건드리지 않습니다.
따라서 문자열 조립 단계에는 lock이 필요하지 않습니다.

반면 실제 출력은 공유 자원인 `std::cout`에 접근하므로,
이 구간만 mutex로 보호합니다.

```cpp
std::lock_guard<std::mutex> lock(output_mutex_);
std::cout << oss.str();
```

즉, 동작 순서는 다음과 같습니다.

```text
로그 문자열 조립
↓
mutex 획득
↓
완성된 문자열 출력
↓
mutex 해제
```

---

### 15-3. lock 점유 시간 최소화

락을 잡은 상태에서 문자열을 조립하면,
로그 포맷 생성 시간까지 lock 점유 시간에 포함됩니다.

`LineLogger`는 문자열 조립을 lock 밖에서 수행하고,
실제 `std::cout` 출력 시점에만 lock을 잡습니다.

따라서 lock 안에서 수행되는 작업은 다음으로 제한됩니다.

```text
완성된 로그 문자열 하나를 std::cout에 출력한다.
```

이 구조는 다음을 목표로 합니다.

- 불필요한 lock 점유 시간 감소
- 로그 출력에 따른 스레드 대기 시간 감소
- 멀티스레드 환경에서 로그 출력 병목(락 경합) 완화

---

### 15-4. 출력 연산 최소화

기존 출력 방식은 하나의 로그를 여러 번의 `operator<<()` 호출로 출력합니다.

반면 `LineLogger`는 완성된 로그 문자열 하나를 다음 한 번의 출력 연산으로 전달합니다.

```cpp
std::cout << oss.str();
```

이 설계는 다음을 의도합니다.

- 출력 연산 횟수 감소
- 출력 구간 단순화
- 로그 조각 사이에 다른 출력이 끼어들 여지 감소
- lock으로 보호해야 하는 실제 출력 구간 축소

---

### 15-5. 로그 타입 제한

`LineLogger`는 로그 타입을 문자열이 아니라 `LogType` enum class로 받습니다.

```cpp
enum class LogType {
    CONNECTED,
    RECEIVING,
    RECV_COMPLETE,
    SENDING,
    SEND_COMPLETE,
    DISCONNECTED,
    PROTOCOL_ERROR,
    TRANSPORT_ERROR,
    RECEIVE_ERROR_PACKET,
    SEND_ERROR_PACKET
};
```

이를 통해 로그 타입 오타를 줄이고,
프로젝트에서 사용할 수 있는 로그 타입을 코드 레벨에서 제한합니다.

---

### 15-6. 정리

`LineLogger`의 로그 출력 동기화 설계는 다음으로 요약할 수 있습니다.

```text
전역 단일 LineLogger 객체
+ 전역 단일 output_mutex_
+ 로그 문자열 선조립
+ 출력 시점만 lock
+ 완성된 문자열 하나를 한 번의 operator<<로 출력
```

이 구조를 통해 프로젝트는 로그 한 줄을 하나의 의미 단위로 다루고,
멀티스레드 환경에서 로그가 섞이는 문제를 줄이는 것을 목표로 합니다.

---

### 15-7. Logging 계층 분리

현재 프로젝트는 로그 출력 계층을 다음과 같이 구분합니다.

```text
ClientSession
  → Session 계층 로그

send_all()
recv_all()
  → Transport 계층
````

현재 구현된 `WriteSessionLog()`는
`SessionID`, `IP`, `Port`를 알고 있는 `ClientSession` 계층에서 사용합니다.

반면 `send_all()`과 `recv_all()`은
클라이언트 세션을 직접 알지 못하는 low-level transport 계층입니다.

따라서 현재 구조에서는 transport 계층에서
`SessionID` 기반 로그를 직접 출력하지 않습니다.

필요하다면 추후

```cpp
WriteTransportLog(...)
```

형태의 별도 함수를 추가하여

```text
[TRANSPORT][SENDING]
[TRANSPORT][RECEIVING]
```

형식의 로그를 출력하도록 확장할 수 있습니다.

현재 단계에서는 아직 구현하지 않았으며,
향후 확장 후보로 남겨둡니다.

---
## 16. 아직 남은 정책 결정

Broadcast 단계에서 아직 결정해야 할 정책은 다음과 같습니다.

- sender 자신에게도 메시지를 보낼 것인가?
- `SendPacket()` 실패 시 즉시 `closing = true`로 바꿀 것인가?
- 송신 실패한 세션을 Broadcast 내부에서 제거할 것인가?
- 제거 요청은 즉시 수행할 것인가, 별도 정리 단계에서 수행할 것인가?
- 느린 클라이언트가 있을 때 send timeout 또는 send queue를 둘 것인가?
- detach 기반 구조를 계속 유지할 것인가, thread 관리 모델을 바꿀 것인가?

현재 단계에서는 Echo Server 구조 안정화가 우선입니다.

---

## 17. 향후 send queue 구조 검토

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

## 18. 정리

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
