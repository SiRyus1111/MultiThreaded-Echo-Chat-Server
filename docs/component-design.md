# Component Design

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

docs/architecture.md
  → 전체 구조와 책임 분리

docs/protocol.md
  → 패킷 구조, TCP byte stream, 송수신 흐름

docs/socket-raii.md
  → Winsock2 socket 자원 수명 관리

docs/concurrency-design.md
  → 멀티스레딩, 객체 소유권, 세션 생명주기, 동기화 설계

docs/roadmap.md
  → 구현 완료 / 구현 예정 / 향후 개선 계획

docs/component-design.md
  → 주요 클래스의 멤버 변수와 핵심 함수의 설계 의도
```

즉, 이 문서는 `architecture.md`와 `protocol.md` 사이를 이어주는 문서입니다.

`architecture.md`가 “전체적으로 누가 어떤 책임을 가지는가?”를 설명한다면,
이 문서는 “그 책임을 수행하기 위해 각 클래스와 함수가 왜 이런 형태를 가지는가?”를 설명합니다.

---

## 2. 설계 기준

이 프로젝트의 컴포넌트 설계 기준은 다음과 같습니다.

```text
1. 클라이언트 한 명을 ClientSession 하나로 표현한다.
2. ClientSession은 해당 클라이언트의 socket, 상태, 송수신 흐름을 가진다.
3. ClientManager는 여러 ClientSession의 목록과 관계를 관리한다.
4. ClientManager는 SessionID를 key로 사용해 세션을 식별한다.
5. 패킷 송수신 절차는 한 패킷 단위로, RecvPacket() / SendPacket()으로 분리한다.
6. Run()은 Echo Server의 고수준 흐름만 제어한다.
7. 송수신 결과는 bool 하나가 아니라 NetState로 표현한다.
8. ClientSession의 물리적 생존, ClientSession의 논리적 종료, ClientSession에 대한 ClientManager의 관리 목록 제거를 분리한다.
```

특히 다음 구분이 중요합니다.

```text
shared_ptr
  → 객체의 물리적 생존 보장

closing
  → 세션의 논리적 종료 상태

RemoveClient()
  → ClientManager의 관리 목록에서 제거
```

객체가 아직 살아있다는 것과,
그 객체가 정상적인 송수신 대상으로 남아있다는 것은 서로 다른 문제입니다.

---

# 3. ClientSession

## 3-1. 역할

`ClientSession`은 클라이언트 한 명의 연결 단위를 표현하는 객체입니다.

즉, 클라이언트 한 명과 관련된 다음 요소를 하나의 객체로 묶습니다.

- socket
- 주소 정보
- 로그 출력용 주소 문자열
- 송수신 상태
- 논리적 종료 상태
- 패킷 수신 함수
- 패킷 송신 함수
- Echo Server의 고수준 실행 흐름
- 통신 종료 / 예외 후처리 흐름

현재 설계에서 `ClientSession`은 단순한 데이터 묶음이 아닙니다.

```text
ClientSession
  → 클라이언트 한 명의 생명주기와 통신 흐름을 표현하는 객체
```

---

## 3-2. 현재 구조

현재 `ClientSession`의 핵심 구조는 다음과 같습니다.

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

현재 `session_id`는 `ClientManager`가 세션을 식별하기 위한 서버 내부 ID입니다.
세션 제거 시 `shared_ptr<ClientSession>`을 직접 대조하지 않고,
`SessionID`를 key로 사용하여 `ClientManager::RemoveClient(session_id)`를 호출합니다.

# 4. ClientSession 멤버 변수 설계

## 4-1. ClientSock

```cpp
std::unique_ptr<ClientSocket> ClientSock;
```

`ClientSock`은 해당 클라이언트와 연결된 socket을 소유합니다.

`ClientSocket`은 raw `SOCKET`을 감싼 RAII 객체이며,
실제 socket 자원은 특정 클라이언트 세션 하나에만 속하는 것이 자연스럽습니다.

따라서 `ClientSession`은 `ClientSocket`을 `unique_ptr`로 독점 소유합니다.

```text
ClientSession
  └── unique_ptr<ClientSocket>
        └── SOCKET
```

이 구조의 의미는 다음과 같습니다.

```text
특정 클라이언트 socket의 소유자는 해당 ClientSession 하나다.
```

`ClientSession`이 소멸하면,
소유하고 있던 `ClientSocket`도 함께 소멸하고,
`ClientSocket`의 소멸자에서 raw `SOCKET`이 정리됩니다.

---

## 4-2. ClientAddr

```cpp
sockaddr_in ClientAddr;
```

`ClientAddr`은 클라이언트의 주소 정보를 저장합니다.

이 값은 socket처럼 직접 닫아야 하는 자원이 아니라,
클라이언트 주소를 나타내는 값 데이터에 가깝습니다.

따라서 현재는 `ClientSession` 내부에서 값으로 보관합니다.

추후 IPv6까지 고려하게 되면 `sockaddr_storage`로 확장할 수 있습니다.

---

## 4-3. ClientAddrStr

```cpp
char ClientAddrStr[INET_ADDRSTRLEN];
```

`ClientAddrStr`은 로그 출력을 위한 클라이언트 IP 문자열입니다.

접속한 클라이언트의 `IP:Port`를 출력할 때,
`sockaddr_in` 구조체나 `in_addr` 구조체만으로는 원하는 형식의, 10진수의 `IP`를 출력할 수 없습니다.

그리고 멀티클라이언트 환경에서는 여러 클라이언트의 로그가 섞이기 쉽습니다.

예를 들어 다음 로그만으로는 어떤 클라이언트의 로그인지 구분하기 어렵습니다.

```text
[RecvPacket] header received
[SendPacket] packet sent
[Close] peer exit
```

따라서 `ClientSession` 생성 시점에 클라이언트 IP 문자열을 저장해두고,
로그 출력 시 이를 함께 사용할 수 있도록 합니다.

예상 로그 형식은 다음과 같습니다.

```text
[127.0.0.1][RecvPacket] header received
[127.0.0.1][SendPacket] packet sent
[127.0.0.1][Close] peer exit
```

로그에 `SessionID`가 도입되면 다음처럼 확장할 수 있습니다.

```text
[Session 3][127.0.0.1:53021][RecvPacket] header received
```

---

## 4-4. Manager_wp

```cpp
std::weak_ptr<ClientManager> Manager_wp;
```

`ClientSession`은 자신을 관리하는 `ClientManager`에 접근해야 할 수 있습니다.

예를 들면 다음 상황입니다.

- 접속 종료 시 자신을 `ClientManager` 목록에서 제거 요청
- 추후 채팅 메시지를 `ClientManager::Broadcast()`로 전달
- 서버 공용 상태 또는 클라이언트 목록에 접근

하지만 `ClientSession`이 `ClientManager`를 소유해서는,
즉 `ClientSession`이 `ClientManager`의 수명에 영향을 주면 안 됩니다.

`ClientManager`는 여러 `ClientSession`을 관리하고,
`ClientSession`은 필요할 때만 `ClientManager`를 참조합니다.

따라서 `ClientSession`은 `ClientManager`를 `weak_ptr`로 참조합니다.

```text
ClientManager
  └── unordered_map<SessionID, shared_ptr<ClientSession>>

ClientSession
  └── weak_ptr<ClientManager>
```

이 구조는 순환 참조를 막기 위한 설계입니다.

실제로 `ClientManager`에 접근할 때는 `lock()`을 사용합니다.

```cpp
// RemoveThisClient() 함수 예시
if (auto manager = Manager_wp.lock()) {
    manager->RemoveClient(session_id);
}
```

`lock()`이 실패하면 `ClientManager`가 이미 소멸된 것이므로,
해당 상황에서는 `ClientManager`에 접근하지 않습니다.

SessionID 기반 제거 구조에서는 `RemoveThisClient()`가 더 이상
`shared_from_this()`로 자기 자신의 `shared_ptr`을 넘길 필요가 없습니다.
대신 `ClientSession`이 값으로 보관하는 `session_id`를 `ClientManager`에게 넘깁니다.

## 4-5. ClientState

```cpp
NetState ClientState;
```

`ClientState`는 해당 클라이언트 세션의 송수신 상태를 저장합니다.

멀티스레드 서버에서는 각 클라이언트의 통신 상태가 서로 독립적이어야 합니다.

따라서 `NetState`는 전역 상태가 아니라,
각 `ClientSession`이 자기 자신의 값으로 소유합니다.

```text
ClientSession A
  └── NetState A

ClientSession B
  └── NetState B
```

`NetState`가 표현하는 상태는 다음과 같습니다.

- header 수신 여부
- payload 수신 여부
- header 송신 여부
- payload 송신 여부
- transport error 발생 여부
- peer exit 발생 여부
- protocol error 발생 여부
- peer error 패킷 수신 여부

이 상태를 통해 `Run()`은 단순 성공 / 실패 이상의 정보를 바탕으로
다음 흐름을 결정할 수 있습니다.

---

## 4-6. closing

```cpp
std::atomic<bool> closing = false;
```

`closing`은 `ClientSession`의 논리적 종료 상태를 나타냅니다.

의미는 다음과 같습니다.

```text
closing == false
  → 아직 정상 송수신 가능한 세션

closing == true
  → 객체는 아직 살아있지만, 더 이상 정상적인 송신 대상으로 보지 않는 종료 예정 세션
```

`closing`은 객체 소멸 여부를 나타내지 않습니다.

객체의 실제 생존 여부는 `shared_ptr`의 reference count로 관리됩니다.

```text
shared_ptr
  → 물리적 생존

closing
  → 논리적 종료 상태
```

`closing`을 분리하는 이유는,
멀티스레드 환경에서 “객체가 살아있음”과 “정상 송신 대상으로 사용 가능함”이 다르기 때문입니다.

예를 들어 `ClientManager`에서 제거되었더라도,
detached client thread가 아직 `shared_ptr<ClientSession>`을 들고 있으면
`ClientSession` 객체는 살아있을 수 있습니다.

반대로 객체가 살아있더라도,
이미 peer exit나 transport error가 발생한 세션이라면
새로운 broadcast 대상에서는 제외되어야 합니다.

---

## 4-7. session_id

```cpp
SessionID session_id;
```

`session_id`는 `ClientSession`을 구분하기 위한 서버 내부 식별자입니다.

IP:Port만으로도 어느 정도 클라이언트를 구분할 수는 있지만,
세션 관리 기준으로는 부족할 수 있습니다.

예를 들어 다음 상황에서는 별도의 세션 ID가 있는 편이 더 명확합니다.

- `ClientManager`가 특정 세션을 빠르게 찾고 제거해야 하는 경우
- 로그에서 여러 클라이언트의 출력이 섞이는 경우
- 추후 `unordered_map` 기반 key-value 구조로 세션 목록을 관리하는 경우
- 같은 IP에서 여러 클라이언트가 접속하는 경우

따라서 현재 설계에서는 `main thread`가 `accept()` 직후 세션 ID를 부여하고,
`ClientSession` 생성자에서 해당 값을 받아 내부에 저장합니다.

```cpp
using SessionID = uint64_t;

const SessionID INITIAL_SESSION_ID = 0;
```

`uint64_t`를 사용하는 이유는 세션 ID 고갈 가능성을 사실상 무시할 수 있을 정도로 크게 잡기 위해서입니다.
`uint32_t`도 일반적인 테스트 환경에서는 충분하지만,
세션 ID는 서버 내부에서 계속 증가하는 값이므로 안전하게 64비트 부호 없는 정수를 사용합니다.

현재 구조에서 세션 ID 부여는 `main thread`의 accept loop에서만 수행합니다.
따라서 단일 accept thread 구조에서는 별도의 atomic이나 mutex 없이도
세션 ID 증가 과정에서 레이스 컨디션이 발생하지 않습니다.

```cpp
SessionID next_session_id = INITIAL_SESSION_ID;

while (true) {

    auto session = std::make_shared<ClientSession>(
        std::move(client_socket),
        client_addr,
        session_id
    );

    manager->AddClient(session, session_id);

    next_session_id += 1;
}
```

단, 추후 여러 thread가 accept를 수행하거나,
여러 위치에서 세션 ID를 발급하는 구조가 된다면
`next_session_id`는 `std::atomic<SessionID>` 또는 별도 mutex로 보호해야 합니다.


# 5. ClientSession 핵심 함수 설계

## 5-1. ClientSession 생성자

```cpp
ClientSession(std::unique_ptr<ClientSocket> s, sockaddr_in addr, SessionID id);
```

생성자는 `ClientSocket`의 소유권, 클라이언트 주소 정보, 서버 내부 세션 ID를 받아
하나의 `ClientSession`으로 묶습니다.

핵심은 다음 세 가지입니다.

```text
1. ClientSocket의 소유권 이동
2. 클라이언트 주소 정보 저장
3. SessionID를 해당 객체에 저장
```

그리고 `inet_ntop()`로 `ClientAddr`을 사용해서 `ClientAddrStr`의 값을 설정합니다.

```cpp
ClientSession::ClientSession(std::unique_ptr<ClientSocket> s, sockaddr_in addr, SessionID id)
    : ClientSock(std::move(s)),
      ClientAddr(addr),
      ClientAddrStr{},
      ClientState{},
      closing(false),
      session_id(id) {
    inet_ntop(AF_INET, &ClientAddr.sin_addr, ClientAddrStr, sizeof(ClientAddrStr));
}
```

`std::unique_ptr<ClientSocket>`은 복사될 수 없으므로,
생성자 내부에서 멤버 변수로 넘길 때 `std::move(s)`가 필요합니다.

이 구조의 의미는 다음과 같습니다.

```text
accept()로 얻은 client socket
  → ClientSocket RAII 객체로 감싼다
  → unique_ptr<ClientSocket>으로 ClientSession에게 소유권을 넘긴다
  → main thread가 부여한 SessionID를 ClientSession에 저장한다
  → 이후 해당 socket과 SessionID는 ClientSession에 속한다
```

`SessionID`는 socket 자원이 아니라 값 데이터입니다.
따라서 `ClientSession` 내부에서 단순 값으로 보관합니다.

## 5-2. AddToManager()

```cpp
void AddToManager(std::shared_ptr<ClientManager> manager_sp);
```

`AddToManager()`는 `ClientSession`이 자신을 관리하는 `ClientManager`를 참조할 수 있도록
`weak_ptr<ClientManager>`를 설정하는 함수입니다.

```cpp
void ClientSession::AddToManager(std::shared_ptr<ClientManager> manager_sp) {
    Manager_wp = manager_sp;
}
```

이 함수가 필요한 이유는 다음과 같습니다.

```text
ClientSession은 ClientManager에게 제거 요청을 해야 한다.
하지만 ClientManager를 소유해서는 안 된다.
따라서 shared_ptr을 받아 weak_ptr로 저장한다.
```

즉, `AddToManager()`는 소유권을 받는 함수가 아니라,
비소유 참조 관계를 설정하는 함수입니다.

---

## 5-3. Run()

```cpp
void Run();
```

`Run()`은 해당 클라이언트 세션의 고수준 실행 흐름을 담당합니다.

현재 5.20 리팩토링 이후,
`Run()`은 Header / Payload 송수신 세부 절차를 직접 담당하지 않습니다.

대신 다음 흐름을 제어합니다.

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

`Run()`의 설계 의도는 다음입니다.

```text
Run()
  → Echo Server의 고수준 흐름 제어

RecvPacket()
  → PacketHeader + Payload 수신 세부 절차

SendPacket()
  → PacketHeader + Payload 송신 세부 절차

TransportExceptionHandling()
  → 종료 / 예외 상태별 후처리
```

이렇게 분리하면 `Run()`은 다음 세부사항을 직접 알 필요가 없습니다.

- network byte order 변환
- PacketHeader 수신 절차
- payload length 검증
- PacketType 검증
- payload 수신 절차
- PacketHeader 송신 절차
- Header / Payload 송신 순서
- 송수신 중 발생한 세부 상태 기록

대신 `Run()`은 `RecvPacket()`과 `SendPacket()`이 반환한 `NetState`를 보고
계속 진행할지, 종료 처리할지 결정합니다.

이 구조는 추후 Chat Server 확장에도 유리합니다.

Echo Server 단계에서는 다음 흐름입니다.

```text
RecvPacket()
  → SendPacket()
```

Chat Server 단계에서는 다음 흐름으로 바뀔 수 있습니다.

```text
RecvPacket()
  → ClientManager::Broadcast()
```

즉, 메시지 처리 방식이 echo에서 broadcast로 바뀌더라도,
패킷 수신 / 송신 함수의 기본 구조는 재사용할 수 있습니다.

---

## 5-4. RecvPacket()

```cpp
NetState RecvPacket(char* buf);
```

`RecvPacket()`은 하나의 완전한 패킷을 수신하는 함수입니다.

기본 흐름은 다음과 같습니다.

```text
1. PacketHeader 수신
2. network byte order → host byte order 변환
3. length 검사
4. type 검사
5. payload 길이만큼 payload 수신
6. 문자열 출력용 null 문자 추가
7. HEADER_ERROR 패킷이면 peer error로 기록
8. 수신 과정에서 발생한 상태를 NetState로 반환
```

### RecvPacket()이 NetState를 반환하는 이유

`RecvPacket()`은 단순히 payload만 반환하는 함수가 아닙니다.

패킷 수신 과정에서는 다음과 같은 상태가 함께 발생할 수 있습니다.

- header 수신 성공
- header 수신 중 transport error
- header 수신 중 peer exit
- payload 수신 성공
- payload 수신 중 transport error
- payload 수신 중 peer exit
- length가 허용 범위를 벗어난 protocol error
- type이 허용되지 않은 protocol error
- 상대가 보낸 `HEADER_ERROR` 패킷 수신

이 상태들은 단순한 `bool` 반환값으로 표현하기 어렵습니다.

예를 들어 `false` 하나만 반환하면,
다음 상황을 구분하기 어렵습니다.

```text
false
  → recv()에서 SOCKET_ERROR가 난 것인가?
  → recv() == 0으로 peer exit이 발생한 것인가?
  → length 검증 실패로 protocol error가 발생한 것인가?
  → HEADER_ERROR 패킷을 받은 것인가?
```

따라서 `RecvPacket()`은 수신 결과를 `NetState`로 반환합니다.

```text
Run()
  → RecvPacket()
  → 반환된 NetState를 보고 다음 흐름 결정
```

이 구조의 장점은 다음과 같습니다.

```text
RecvPacket()
  → 패킷 수신의 세부 절차와 상태 기록 담당

Run()
  → 반환된 NetState를 바탕으로 고수준 흐름 결정
```

즉, `Run()`은 Header / Payload 수신 절차,
byte order 변환, length 검증 같은 세부 구현을 직접 알 필요가 없습니다.

대신 반환된 `NetState`를 통해
통신을 계속할지, 종료 처리할지, protocol error로 처리할지를 판단합니다.

### 왜 ClientState도 있는데 NetState를 반환하는가?

`ClientSession`은 멤버로 `ClientState`를 가지고 있습니다.

그런데도 `RecvPacket()`이 `NetState`를 반환하는 이유는,
함수 호출 결과를 호출자가 즉시 확인할 수 있게 하기 위함입니다.

```text
ClientState
  → 세션 내부에 누적되는 현재 통신 상태

RecvPacket()의 반환값
  → 이번 수신 호출의 결과를 Run()에게 명시적으로 전달
```

즉, 반환값은 `Run()`에게 “이번 호출에서 어떤 일이 일어났는지”를 알려주는 역할을 합니다.

이렇게 하면 `Run()`의 흐름이 다음처럼 명확해집니다.

```cpp
NetState recv_state = RecvPacket(buf);

if (recv_state.if_error ||
	  recv_state.if_header_error ||
	  recv_state.if_peer_error ||
	  recv_state.if_peer_exit) {
	  MarkClosing();
	  TransportExceptionHandling();
	  RemoveThisClient();

	  break;
}
```

---

## 5-5. SendPacket()

```cpp
NetState SendPacket(const char* msg, uint32_t len, PacketType type);
```

`SendPacket()`은 하나의 완전한 패킷을 송신하는 함수입니다.

기본 흐름은 다음과 같습니다.

```text
1. payload 길이 검사
2. PacketHeader 구성
3. host byte order → network byte order 변환
4. PacketHeader 송신
5. Payload 송신
6. 송신 과정에서 발생한 상태를 NetState로 반환
```

### SendPacket()이 NetState를 반환하는 이유

송신 과정에서도 단순히 성공 / 실패만 있는 것이 아닙니다.

다음 상태를 구분해야 합니다.

- payload 길이 검증 성공
- payload 길이가 허용 범위를 벗어난 protocol error
- header 송신 성공
- header 송신 중 transport error
- payload 송신 성공
- payload 송신 중 transport error

예를 들어 payload 길이가 `0`이거나 `PAYLOAD_SIZE`를 초과하면,
실제 `send()`를 호출하기 전에 protocol error로 처리할 수 있습니다.

반대로 `send()` 중 `SOCKET_ERROR`가 발생하면,
이는 protocol error가 아니라 transport error입니다.

따라서 `SendPacket()`도 `bool` 하나만 반환하기보다,
송신 과정에서 어떤 상태가 발생했는지를 `NetState`로 반환합니다.

```text
Run()
  → SendPacket()
  → 반환된 NetState를 보고 종료 / 계속 진행 여부 결정
```

### SendPacket()과 send_mutex

현재 `SendPacket()`은 Header와 Payload를 하나의 논리적 패킷으로 보냅니다.

```text
PacketHeader 송신
Payload 송신
```

추후 Broadcast Chat Server 단계에서는 여러 thread가 같은 `ClientSession`에 대해
`SendPacket()`을 호출할 가능성이 있습니다.

이때 다음과 같은 순서가 발생하면 안 됩니다.

```text
잘못된 예:

Header_A
Header_B
Payload_A
Payload_B
```

원하는 순서는 다음과 같습니다.

```text
올바른 예:

Header_A
Payload_A
Header_B
Payload_B
```

따라서 추후 `ClientSession`별 `send_mutex`는
`SendPacket()` 내부에 적용하는 것이 자연스럽습니다.

```cpp
// 추후 적용 예정
NetState ClientSession::SendPacket(const char* msg, uint32_t len, PacketType type) {
    std::lock_guard<std::mutex> lock(send_mutex);

    // Header 구성
    // Header 송신
    // Payload 송신
}
```

이렇게 하면 같은 클라이언트에게 보내는 Header / Payload 순서를 보호할 수 있습니다.

---

## 5-6. MarkClosing()

```cpp
void MarkClosing();
```

`MarkClosing()`은 `ClientSession`의 논리적 종료 상태를 표시하는 함수입니다.

내부적으로는 `closing`을 `true`로 바꾸는 역할입니다.

```text
MarkClosing()
  → closing = true
```

이 함수를 따로 두는 이유는,
`closing`이라는 멤버를 외부에서 직접 변경하지 않도록 하기 위해서입니다.

즉, 종료 상태 전환을 명시적인 함수 호출로 표현합니다.

```text
Run()
  → 오류 또는 종료 상황 감지
  → MarkClosing()
  → TransportExceptionHandling()
```

이 구조를 사용하면 나중에 `closing`을 바꾸는 순간에
추가 작업을 넣기도 쉬워집니다.

예를 들어 추후 다음 작업이 필요해질 수 있습니다.

- 종료 로그 출력
- 종료 사유 저장
- 중복 종료 처리 방지
- 통계 정보 갱신
- send queue 정리

따라서 단순히 `closing = true`를 직접 여러 곳에 흩뿌리기보다,
`MarkClosing()`으로 종료 상태 전환 지점을 모으는 것이 좋습니다.

---

## 5-7. TransportExceptionHandling()

```cpp
void TransportExceptionHandling();
```

`TransportExceptionHandling()`은 통신 종료 / 예외 상황이 발생한 뒤
상태별 후처리 흐름을 담당하는 함수입니다.

현재 `Run()`은 `RecvPacket()` / `SendPacket()`의 반환값을 보고
다음 상황을 감지할 수 있습니다.

- transport error
- peer exit
- protocol error
- peer error

이때 `Run()` 안에서 모든 예외 후처리를 직접 수행하면,
`Run()`이 다시 길고 복잡해질 수 있습니다.

따라서 `Run()`은 고수준 흐름만 담당하고,
실제 후처리 흐름은 `TransportExceptionHandling()`으로 분리합니다.

```text
Run()
  → 송수신 상태 확인
  → 종료 / 예외 상황 감지
  → MarkClosing()
  → TransportExceptionHandling()
```

이 함수의 설계 의도는 다음입니다.

```text
통신 종료 / 예외 상황의 후처리를 한 곳으로 모은다.
Run()이 다시 예외 처리 코드로 비대해지는 것을 막는다.
```

추후 이 함수는 다음 작업을 담당할 수 있습니다.

- transport error 로그 출력
- peer exit 로그 출력
- protocol error 로그 출력
- `RemoveThisClient()` 호출
- 종료 사유별 분기
- 서버 공용 상태 갱신

---

## 5-8. RemoveThisClient()

```cpp
void RemoveThisClient();
```

`RemoveThisClient()`는 현재 `ClientSession`이 자신을 관리하는 `ClientManager`에게
자기 자신을 관리 목록에서 제거해달라고 요청하는 함수입니다.

SessionID 도입 이후의 개념적 흐름은 다음과 같습니다.

```text
ClientSession
  → Manager_wp.lock()
  → ClientManager가 살아있으면 RemoveClient(session_id) 호출
  → ClientManager가 소멸했으면 접근하지 않음
```

구조는 다음과 같습니다.

```cpp
void ClientSession::RemoveThisClient() {
    if (auto manager = Manager_wp.lock()) {
        manager->RemoveClient(session_id);
    }
    else {
        std::cout << "ClientManager 객체 이미 소멸됨. RemoveClient()가 실행되지 않습니다.\n";
    }
}
```

이 함수의 설계 의도는 다음입니다.

```text
ClientSession이 종료될 때,
자신의 SessionID를 기준으로
ClientManager에게 관리 목록 제거를 요청한다.
```

여기서 중요한 점은 다음입니다.

```text
RemoveThisClient()
  → ClientManager에 제거 요청, 즉 ClientManager::RemoveClient(session_id) 호출 요청

ClientManager::RemoveClient(SessionID id)
  → clients unordered_map에서 해당 key 제거

ClientSession 소멸
  → 마지막 shared_ptr이 사라질 때 발생
```

즉, `RemoveThisClient()`는 객체를 직접 삭제하는 함수가 아닙니다.

`ClientManager`의 관리 목록에서 자신을 제외하도록 요청하는 함수입니다.

또한 이전 구조처럼 `shared_from_this()`로 자기 자신의 `shared_ptr`을 넘기지 않습니다.
`ClientManager`가 `SessionID`를 key로 세션을 관리하므로,
세션 제거 요청에는 `session_id`만 있으면 충분합니다.

따라서 만약 `ClientSession` 내부의 다른 함수에서도 `shared_from_this()`를 사용하지 않는다면,
장기적으로는 `ClientSession`의 `std::enable_shared_from_this<ClientSession>` 상속 필요성도 다시 검토할 수 있습니다.

# 6. ClientManager

## 6-1. 역할

`ClientManager`는 현재 접속 중인 여러 `ClientSession`을 관리하는 객체입니다.

`ClientSession`이 클라이언트 한 명의 단위라면,
`ClientManager`는 여러 세션의 목록과 관계를 관리합니다.

역할은 다음과 같습니다.

- 새 `ClientSession` 추가
- 종료된 `ClientSession` 제거
- 현재 접속 중인 클라이언트 목록 관리
- clients 컨테이너 동기화
- 추후 broadcast 수행
- 추후 `SessionID` 기반 세션 조회

현재 구조는 다음과 같습니다.

```cpp
class ClientManager : public std::enable_shared_from_this<ClientManager> {
private:
    std::unordered_map<SessionID, std::shared_ptr<ClientSession>> clients;
    std::mutex clients_mutex;

public:
    void AddClient(std::shared_ptr<ClientSession> client);
    void RemoveClient(std::shared_ptr<ClientSession> client);
};
```

---

## 6-2. clients

```cpp
std::unordered_map<SessionID, std::shared_ptr<ClientSession>> clients;
```

`clients`는 현재 접속 중인 `ClientSession` 목록입니다.

이제 `clients`는 단순한 순차 목록이 아니라,
`SessionID`를 key로 사용하는 key-value 컨테이너입니다.

```text
key   = SessionID
value = shared_ptr<ClientSession>
```

`shared_ptr<ClientSession>`을 저장하는 이유는
`ClientManager`가 세션의 물리적 생존에 참여하기 때문입니다.

```text
ClientManager가 clients에 shared_ptr을 보관 중이면
ClientSession 객체는 소멸하지 않는다.
```

하지만 이것이 `ClientManager`만 세션을 소유한다는 의미는 아닙니다.

현재 구조에서는 detached client thread도 `shared_ptr<ClientSession>`을 들고 있습니다.

```text
ClientManager
  └── unordered_map<SessionID, shared_ptr<ClientSession>>

client_thread()
  └── shared_ptr<ClientSession>
```

따라서 `ClientManager`에서 세션을 제거하더라도,
client thread가 아직 실행 중이면 세션 객체는 즉시 소멸하지 않습니다.

`unordered_map`을 사용하는 이유는 다음과 같습니다.

- `SessionID`를 기준으로 세션을 직접 찾을 수 있다.
- `RemoveClient(SessionID id)`를 key 기반으로 구현할 수 있다.
- `shared_ptr<ClientSession>`을 직접 대조하지 않아도 된다.
- 추후 broadcast, 로그, 세션 조회 구조를 SessionID 중심으로 확장하기 쉽다.

## 6-3. clients_mutex

```cpp
std::mutex clients_mutex;
```

`clients_mutex`는 `clients` 컨테이너를 보호하기 위한 mutex입니다.

멀티스레드 서버에서는 다음 작업이 동시에 발생할 수 있습니다.

- main thread가 새 클라이언트를 추가
- client thread가 종료되며 자신을 제거
- 추후 broadcast가 현재 clients 목록을 순회 또는 snapshot 복사

따라서 `clients`에 접근하는 구간은 mutex로 보호해야 합니다.

보호 대상은 다음입니다.

```text
clients 컨테이너의 구조 변경
  → push_back
  → erase
  → snapshot 복사
```

중요한 점은 `clients_mutex`가 모든 송신 작업을 보호하는 mutex가 아니라는 것입니다.

실제 `send()`는 block될 수 있으므로,
추후 Broadcast 단계에서는 `clients_mutex`를 잡은 상태에서 `send()`를 오래 수행하지 않는 구조를 목표로 합니다.

---

# 7. ClientManager 핵심 함수 설계

## 7-1. AddClient()

```cpp
void AddClient(std::shared_ptr<ClientSession> client, SessionID id);
```

`AddClient()`는 새로 생성된 `ClientSession`을 `ClientManager`의 관리 목록에 추가하는 함수입니다.

기본 흐름은 다음과 같습니다.

```text
1. ClientSession에 ClientManager 참조를 설정한다.
2. clients_mutex를 잡는다.
3. clients unordered_map에 SessionID를 key로 shared_ptr<ClientSession>을 추가한다.
```

예상 구조는 다음과 같습니다.

```cpp
void ClientManager::AddClient(std::shared_ptr<ClientSession> client, SessionID id) {
    client->AddToManager(shared_from_this());

    std::lock_guard<std::mutex> lock(clients_mutex);
    clients[id] = client;
}
```

`AddClient()`에서 중요한 점은
단순히 컨테이너에 값을 넣는 것만이 아니라는 점입니다.

이 함수는 다음 관계를 완성합니다.

```text
ClientManager
  → SessionID를 key로 ClientSession을 shared_ptr로 관리

ClientSession
  → ClientManager를 weak_ptr로 참조
```

따라서 `AddClient()`는
세션 목록 추가와 Manager 참조 설정을 함께 수행하는 함수로 볼 수 있습니다.

추후에는 중복 ID 방지를 더 명확히 하기 위해
`clients.emplace(id, client)`를 사용하고 삽입 성공 여부를 확인하는 방식도 검토할 수 있습니다.

## 7-2. RemoveClient()

```cpp
void RemoveClient(SessionID id);
```

`RemoveClient()`는 특정 `SessionID`에 해당하는 `ClientSession`을
`clients` 컨테이너에서 제거하는 함수입니다.

예상 구조는 다음과 같습니다.

```cpp
void ClientManager::RemoveClient(SessionID id) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    clients.erase(id);
}
```

이 함수에서 가장 중요한 점은 다음입니다.

```text
RemoveClient()는 ClientSession 객체를 즉시 소멸시키는 함수가 아니다.
```

`RemoveClient()`의 의미는 다음입니다.

```text
RemoveClient(SessionID id)
  → ClientManager의 관리 목록에서 제거
  → clients unordered_map에서 해당 key 제거
  → 마지막 shared_ptr이 사라진 경우에만 ClientSession 소멸
```

현재 구조에서는 detached client thread도 `shared_ptr<ClientSession>`을 들고 있습니다.

따라서 `RemoveClient()` 이후에도 client thread가 아직 실행 중이면
`ClientSession` 객체는 살아있을 수 있습니다.

이 구분은 다음 설계와 연결됩니다.

```text
shared_ptr
  → 객체의 물리적 생존 보장

closing
  → 세션의 논리적 종료 상태

RemoveClient()
  → ClientManager의 관리 목록에서 제거
```

SessionID 기반 제거 구조의 장점은 다음과 같습니다.

- `shared_ptr<ClientSession>`을 직접 대조하지 않아도 된다.
- `unordered_map`에서 key 기반 제거가 가능하다.
- `ClientSession::RemoveThisClient()`가 `shared_from_this()`에 의존하지 않아도 된다.
- 로그와 세션 관리 기준을 동일하게 가져갈 수 있다.

## 7-3. Broadcast() [Planned]

```cpp
void Broadcast(...);
```

`Broadcast()`는 추후 Chat Server 단계에서 구현할 함수입니다.

역할은 한 클라이언트가 보낸 메시지를 다른 클라이언트들에게 전달하는 것입니다.

```text
Client A → Server → Client B
                  → Client C
                  → Client D
```

초기 설계 방향은 다음과 같습니다.

```text
1. clients_mutex를 잡는다.
2. 현재 clients 목록의 snapshot을 복사한다.
3. clients_mutex를 해제한다.
4. snapshot을 순회한다.
5. closing == true인 세션은 건너뛴다.
6. 각 ClientSession의 SendPacket()을 호출한다.
```

```cpp
// 예상 구조, 아직 미구현
ClientManager::Broadcast(SessionID session, const char* message, int len) {
	  std::unordered_map<SessionID, std::shared_ptr<ClientSession>> snapshot;
	  {
		    std::lock_guard<std::mutex> lock(client_mutex);
		    snapshot = clients;
	  }

	  for (auto [ID, pSession] : snapshot){
		    if (ID == session) {
			      continue;
		    }
		    if (closing == true) {
			      continue;
		    }

		    pSession->SendPacket(message, len, PacketType::SAFE);
	  }
}
```

이 구조를 사용하는 이유는,
`clients_mutex`를 잡은 상태에서 실제 `send()`를 오래 수행하지 않기 위해서입니다.

`clients_mutex`를 짧게 잡게 된다면, 후일에 발생할 수 있는 데드락 위험과 락 경합을 줄알 수 있습니다.

그리고 `send()`는 block될 수 있으므로,
manager lock을 잡은 상태에서 모든 클라이언트에게 send하면
느린 클라이언트 하나가 전체 서버 흐름에 영향을 줄 수 있습니다.

따라서 `Broadcast()`에서는 clients 목록을 짧게 snapshot으로 복사하고,
실제 송신은 manager lock을 풀고 수행하는 구조를 목표로 합니다.

```text
clients_mutex
  → clients 목록 보호

send_mutex
  → 특정 ClientSession의 Header + Payload 송신 순서 보호
```

---

# 8. LineLogger

## 8-1. 역할

LineLogger는 프로젝트 전반에서 사용하는 공용 로깅 컴포넌트입니다.

근본적인 목적은 다음과 같습니다.

```text
멀티스레드 환경에서
하나의 로그 메시지가
다른 스레드의 출력과 섞이지 않도록 한다.
````

또한 로그 형식을 통일하고,
출력 정책을 한 곳에서 관리할 수 있도록 합니다.

---

## 8-2. 설계 배경

기존에는 다음과 같은 형태의 로그 출력이 사용되었습니다.

```cpp
// 예시
std::cout << "[SessionID " << session_id << "]"
          << "[RECV] "
          << message
          << '\n';
```

하지만 멀티스레드 환경에서는 여러 번의 `<<` 연산 사이에
다른 스레드의 출력이 끼어들 수 있습니다.

따라서 하나의 로그가 여러 조각으로 분리되어 출력될 수 있다.

`LineLogger`는 이를 방지하기 위해

```text
로그 생성
↓
문자열 완성
↓
출력
```

구조를 사용합니다.

---

## 8-3. WriteLog()

```cpp
template<typename... Args>
void WriteLog(Args&&... args);
```

WriteLog()는 전달받은 인자들을
ostringstream로 하나의 문자열로 조립한 뒤 출력합니다.

사용 예시:

```cpp
SessionID session_id = 3

logger.WriteLog(
    "Client ",
    session_id,
    " Connected"
);
```

```text
// 출력
Client 3 Connected
```

---

## 8-4. WriteSessionLog()

```cpp
template<typename... Args>
void WriteSessionLog(
    SessionID sessionId,
    const std::string& ipaddr,
    uint16_t port,
    const std::string& logType,
    Args&&... args
);
```

`ClientSession` 관련 로그를 위한 편의 함수입니다.

다음과 같은 요소들을 입력받아 정해진 형식으로 출력합니다.
- `SessionID`
- `IP`
- `Port`
- `LogType`
- `message`

예상 출력:

```text
[SessionID 3][127.0.0.1:53021][RECV] Hello
```

---

## 8-5. 동기화 전략

LineLogger는 문자열 생성 과정에는 락을 사용하지 않습니다.

```cpp
std::ostringstream oss;
```

를 통해 스레드 로컬 영역에서 문자열을 완성한 뒤,

실제 출력 시점에만 락을 획득합니다.

```cpp
lock
↓
std::cout << 완성된 문자열
↓
unlock
```

이를 통해

* 로그 단위 원자성 확보
* 락 경합 최소화

를 목표로 합니다.

---

## 8-6. 프로젝트 내 역할

프로젝트의 콘솔 출력은
가능한 한 모두 `LineLogger`를 통해 수행합니다.

이를 통해

- 출력 형식 통일
- 로그 정책 중앙 관리
- 추후 파일 로깅 확장
- 로그 레벨 추가

등의 기능을 한 위치에서 관리할 수 있도록 합니다.

---

# 9. Network Helper 함수

## 9-1. send_all()

```cpp
int send_all(SOCKET s, const char* buf, int len, ...);
```

`send_all()`은 요청한 길이만큼 반복해서 `send()`를 호출하는 helper 함수입니다.

`send()` 한 번이 요청한 모든 바이트를 보낸다는 보장은 없습니다.

따라서 다음 구조가 필요합니다.

```text
보내야 할 전체 길이 = len
이미 보낸 길이 = 0

while 이미 보낸 길이 < 전체 길이:
    send()
    성공하면 이미 보낸 길이 증가
    실패하면 transport error
```

`send_all()`의 설계 의도는 다음입니다.

```text
TCP의 partial send 가능성을 호출자에게 숨기고,
호출자는 “요청한 길이 전체를 보내려고 시도한다”는 의미로 사용할 수 있게 한다.
```

---

## 9-2. recv_all()

```cpp
int recv_all(SOCKET s, char* buf, int len, ...);
```

`recv_all()`은 요청한 길이만큼 반복해서 `recv()`를 호출하는 helper 함수입니다.

TCP는 byte stream 기반이므로,
헤더나 payload가 여러 번에 나뉘어 도착할 수 있습니다.

따라서 다음 구조가 필요합니다.

```text
받아야 할 전체 길이 = len
이미 받은 길이 = 0

while 이미 받은 길이 < 전체 길이:
    recv()
    성공하면 이미 받은 길이 증가
    recv() == 0이면 peer exit
    실패하면 transport error
```

`recv_all()`의 설계 의도는 다음입니다.

```text
TCP의 partial recv 가능성을 호출자에게 숨기고,
호출자는 “요청한 길이 전체를 받을 때까지 시도한다”는 의미로 사용할 수 있게 한다.
```

`RecvPacket()`은 이 helper를 사용하여
PacketHeader와 Payload를 각각 정해진 길이만큼 수신합니다.

---

# 10. NetState

## 10-1. 역할

`NetState`는 송수신 과정에서 발생한 상태를 기록하는 구조체입니다.

이 프로젝트에서는 단순히 성공 / 실패만으로는 충분하지 않습니다.

다음 상황을 구분해야 하기 때문입니다.

```text
정상 송수신
transport error
peer exit
protocol error
peer error packet
header만 성공
payload까지 성공
```

따라서 `NetState`는 다음 정보를 담습니다.

- header recv
- payload recv
- header send
- payload send
- transport error
- peer exit
- protocol error
- peer error

---

## 10-2. NetState를 사용하는 이유

`NetState`를 사용하는 이유는
통신 결과를 단순 `bool`보다 더 구체적으로 표현하기 위해서입니다.

```text
bool
  → 성공 / 실패만 표현

NetState
  → 어느 단계에서 어떤 종류의 상태가 발생했는지 표현
```

예를 들어 다음 두 상황은 모두 “실패”로 볼 수 있습니다.

```text
recv()가 SOCKET_ERROR를 반환했다.
상대가 HEADER_ERROR 패킷을 보냈다.
```

하지만 두 상황의 의미는 다릅니다.

```text
SOCKET_ERROR
  → transport level 문제

HEADER_ERROR packet
  → protocol level 또는 peer가 감지한 문제
```

따라서 이 프로젝트는 `NetState`를 통해
통신 결과를 더 세밀하게 표현합니다.

---

## 10-3. RecvPacket() / SendPacket()의 반환값으로 사용하는 이유

`RecvPacket()`과 `SendPacket()`이 `NetState`를 반환하는 이유는,
각 함수가 수행한 송수신 작업의 결과를 `Run()`에게 명시적으로 전달하기 위해서입니다.

```text
RecvPacket()
  → 이번 수신 작업의 상태 반환

SendPacket()
  → 이번 송신 작업의 상태 반환

Run()
  → 반환된 상태를 보고 흐름 결정
```

이 구조에서는 `Run()`이 송수신 세부 구현을 몰라도 됩니다.

대신 다음처럼 상태를 바탕으로 흐름만 판단합니다.

```text
상태가 정상
  → 다음 단계 진행

transport error
  → closing 표시 후 종료 처리

peer exit
  → closing 표시 후 제거 처리

protocol error
  → closing 표시 또는 에러 응답 처리
```

즉, `NetState` 반환은 함수 간 책임 분리를 위한 장치입니다.

```text
RecvPacket() / SendPacket()
  → 송수신 세부 절차와 상태 생성

Run()
  → 상태 기반 고수준 흐름 제어
```

---

# 11. PacketHeader와 PacketType

## 11-1. PacketHeader::type

```cpp
struct PacketHeader {
    int32_t type;
    uint32_t length;
};
```

`PacketHeader::type`은 실제 네트워크 패킷에 저장되는 전송 필드입니다.

따라서 현재 구조에서는 `PacketType`이 아닌`int32_t`로 유지합니다.

```text
PacketHeader::type
  → 네트워크를 통해 전송되는 정수 필드
```

`PacketHeader`의 멤버들은 송신 시 `htonl()`을 적용하고,
수신 시 `ntohl()`을 적용하여 host byte order로 변환합니다.

---

## 11-2. PacketType

```cpp
enum class PacketType : int32_t {
    SAFE = -1,
    HEADER_ERROR = 0
};
```

`PacketType`은 코드 내부에서 패킷 타입의 의미를 표현하기 위한 enum class입니다.

즉, `PacketHeader::type`은 실제 전송 필드이고,
`PacketType`은 코드에서 의미를 표현하기 위한 타입입니다.

```text
PacketHeader::type
  → 값, 전송 필드

PacketType
  → 의미, 코드 내부 표현
```

송신 시에는 `PacketType`을 `int32_t`로 변환한 뒤 `htonl()`을 적용합니다.

```cpp
send_net_header.type = htonl(static_cast<int32_t>(type));
```

수신 시에는 `ntohl()`로 변환한 정수값을
허용된 `PacketType` 값과 비교합니다.

이 구조를 사용하면 전송 형식은 명확하게 유지하면서도,
코드 내부에서는 패킷 타입의 의미를 더 안전하고 읽기 쉽게 표현할 수 있습니다.

---

# 12. 추후 확장 지점

## 12-1. send_mutex

추후 Broadcast Chat Server 단계에서는 여러 thread가
같은 `ClientSession`에 대해 `SendPacket()`을 호출할 수 있습니다.

이때 Header / Payload 순서가 섞이지 않도록
`ClientSession`별 `send_mutex`를 도입할 예정입니다.

```text
send_mutex
  → 특정 ClientSession에 대한 Header + Payload 송신 순서 보호
```

이는 `ClientManager`의 `clients_mutex`와 역할이 다릅니다.

```text
clients_mutex
  → clients 컨테이너 보호

send_mutex
  → 특정 ClientSession에 대한 패킷 송신 순서 보호
```

---

## 12-2. Broadcast()

`Broadcast()`는 Chat Server 확장의 핵심 함수입니다.

이 함수는 단순히 모든 클라이언트에게 메시지를 보내는 함수가 아니라,
다음 동기화 정책을 포함해야 합니다.

- clients 목록을 안전하게 snapshot으로 복사
- `closing == true`인 세션 제외
- 실제 send 중 manager lock을 오래 잡지 않기
- 각 `ClientSession::SendPacket()` 내부에서 send 순서 보호
- 송신 실패한 세션 처리 정책 결정

따라서 `Broadcast()`는 `ClientManager`와 `ClientSession`의 책임 분리를 검증하는 함수가 될 가능성이 큽니다.

---

# 13. 정리

현재 컴포넌트 설계의 핵심은 다음과 같습니다.

```text
ClientSession
  → 클라이언트 한 명의 생명주기와 통신 흐름을 표현한다.

ClientManager
  → 여러 ClientSession의 목록과 관계를 관리한다.

Run()
  → Echo Server의 고수준 흐름을 제어한다.

RecvPacket()
  → PacketHeader + Payload 수신 세부 절차를 담당하고 NetState를 반환한다.

SendPacket()
  → PacketHeader + Payload 송신 세부 절차를 담당하고 NetState를 반환한다.

NetState
  → 송수신 결과를 bool보다 더 세밀하게 표현한다.

closing
  → 세션의 논리적 종료 상태를 나타낸다.

RemoveClient()
  → ClientManager의 관리 목록에서 세션을 제거한다.
```

특히 `RecvPacket()`과 `SendPacket()`이 `NetState`를 반환하는 이유는
패킷 송수신의 세부 절차와 `Run()`의 고수준 흐름 제어를 분리하기 위해서입니다.

```text
RecvPacket() / SendPacket()
  → 세부 송수신 수행 + 상태 생성

Run()
  → 반환된 상태를 바탕으로 흐름 결정
```

이 구조를 통해 현재 Echo Server 단계에서는 `Run()`을 간결하게 유지할 수 있고,
추후 Chat Server 단계에서는 패킷 송수신 함수는 유지한 채
메시지 처리 흐름만 `Broadcast()` 중심으로 확장할 수 있습니다.
