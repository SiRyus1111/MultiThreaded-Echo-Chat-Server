# MultiThreaded Echo Server (WIP: Chat Server) - Winsock2, C++

기존 단일 클라이언트 Echo Server를 기반으로,
멀티클라이언트 / 멀티스레드 Echo Server로 확장한 뒤,
최종적으로 브로드캐스트 기반 Chat Server까지 발전시키는 프로젝트입니다.

> 현재 단계의 목표는 완성된 Chat Server가 아니라,
> **멀티클라이언트 Echo Server를 안정적으로 구성하기 위한 서버 기틀을 만드는 것**입니다.

> 이 프로젝트는 단순히 여러 클라이언트를 동시에 처리하는 데서 끝나지 않고,
> 클라이언트 세션 관리, socket lifetime 관리, 객체 소유권 설계,
> thread-per-client 구조, 공유 컨테이너 동기화, 브로드캐스트 메시지 흐름까지 고려하는 것을 목표로 합니다.

> 기존 프로젝트에서 구현한 TCP byte stream 처리, partial send / recv 대응,
> `PacketHeader` 기반 애플리케이션 레벨 프로토콜,
> OOP / RAII 기반 socket 관리 구조를 바탕으로 멀티클라이언트 서버 구조로 확장합니다.

---

## 0. 현재 구현 상태

현재 이 프로젝트는 **멀티클라이언트 / 멀티스레드 Echo Server의 기본 구조를 구현하는 단계**입니다.

### 구현 완료

* 기존 단일 클라이언트 Echo Server 로직을 `ClientSession` 기반 구조로 이식
* 클라이언트마다 독립적인 `ClientSession` 객체 생성
* 각 `ClientSession`이 자기 자신의 `ClientSocket`, `NetState`, `closing` 상태를 소유
* `ClientManager`가 현재 접속 중인 `ClientSession` 목록 관리
* 클라이언트별 `std::thread` 생성
* detached thread가 `shared_ptr<ClientSession>`을 들고 `Run()` 실행
* 종료 상황 발생 시 `ClientSession`이 `ClientManager`에게 자기 자신 제거 요청
* `shared_ptr` / `weak_ptr` / `unique_ptr` 기반 객체 소유 관계 설계

### 설계 완료 / 구현 예정

* `ClientManager::Broadcast()`
* `ClientSession::SendPacket()` / `ClientSession::RecvPacket()`
* ClientSession별 `send_mutex`
* broadcast 시 clients snapshot 복사 구조
* `SessionID`
* nickname / message type 확장

### 현재 기준 미구현

현재 기준으로 다음 기능은 아직 구현되지 않았습니다.

* Broadcast Chat 기능
* `SessionID`
* nickname 기반 클라이언트 식별
* 메시지 타입별 채팅 프로토콜 분기
* ClientSession별 `send_mutex` 적용
* broadcast 중 송신 실패 세션 정리 정책

---

## 1. 기반 프로젝트

이 프로젝트는 기존 Echo Server 프로젝트를 기반으로 확장합니다.

* 기존 프로젝트: [My First Echo Server Project](https://github.com/SiRyus1111/My-First-Echo-Server-Project)

기존 프로젝트에서는 다음 내용을 구현했습니다.

* TCP 기반 Echo Server / Client
* TCP byte stream 특성을 고려한 partial send / recv 처리
* `send_all()` / `recv_all()` helper 함수
* `PacketHeader(type, length)` 기반 메시지 구조
* protocol error / transport error / peer exit 구분 처리
* `NetState` 기반 통신 상태 관리
* OOP / RAII 기반 socket 자원 관리 구조

본 프로젝트는 위 구조를 기반으로,
단일 클라이언트 Echo Server를 멀티클라이언트 / 멀티스레드 환경으로 확장합니다.

---

## 2. 프로젝트 목표

이 프로젝트의 핵심 목표는 다음과 같습니다.

* 기존 단일 클라이언트 Echo Server 로직을 멀티클라이언트 / 멀티스레드 구조로 확장
* 클라이언트마다 독립적인 thread를 부여하는 thread-per-client 구조 구현
* `ClientSession` 객체를 통해 클라이언트 단위의 상태와 통신 흐름 관리
* `ClientManager` 객체를 통해 접속 중인 클라이언트 목록 관리
* 공유 컨테이너에 대한 mutex 기반 동기화 적용
* 기존 `PacketHeader` 기반 프로토콜 유지 및 확장
* Echo Server 단계 이후 브로드캐스트 Chat Server로 확장
* OOP / RAII 기반 socket lifetime 관리 유지
* 스마트 포인터를 통한 객체 소유권 명확화

---

## 3. 개발 단계

본 프로젝트는 다음 흐름으로 확장할 예정입니다.

```text
기존 단일 클라이언트 Echo Server
        ↓
멀티클라이언트 / 멀티스레드 Echo Server
        ↓
브로드캐스트 Chat Server
```

### 3-1. 기존 단일 클라이언트 Echo Server

기존 프로젝트에서 구현한 구조입니다.

* 하나의 클라이언트 연결 처리
* `PacketHeader` 기반 메시지 송수신
* `send_all()` / `recv_all()` 기반 partial send / recv 처리
* OOP / RAII 기반 socket 자원 관리

### 3-2. 멀티클라이언트 / 멀티스레드 Echo Server

여러 클라이언트가 동시에 서버에 접속할 수 있도록 확장합니다.

목표:

* `accept()` 반복 처리
* 클라이언트별 `ClientSocket` 생성
* 클라이언트별 `ClientSession` 생성
* 클라이언트별 thread 생성
* 각 thread에서 독립적인 Echo 송수신 루프 실행
* `ClientManager`를 통한 접속 클라이언트 목록 관리
* 공유 컨테이너에 대한 동기화 처리

### 3-3. 브로드캐스트 Chat Server

Echo Server 구조를 기반으로,
한 클라이언트가 보낸 메시지를 다른 클라이언트들에게 전달하는 채팅 서버로 확장합니다.

목표:

* `ClientManager`가 전체 클라이언트 목록 관리
* 특정 클라이언트가 보낸 메시지를 다른 클라이언트에게 broadcast
* 접속 종료된 클라이언트 제거
* 공유 컨테이너 동기화
* 추후 닉네임 / 세션 ID / 메시지 타입 확장 가능성 고려

현재 이 단계는 아직 구현 전이며, 설계 아이디어를 정리하는 중입니다.

---

## 4. 프로젝트 구조

예상 프로젝트 구조는 다음과 같습니다.

```text
MultiThreaded Echo-Chat Server
├── Server.cpp
├── Client.cpp
├── include
│   ├── Common.h
│   ├── NetCommon.h
│   └── socketRAII.h
└── README.md
```

### 주요 파일

#### Server.cpp

서버 실행 코드입니다.

역할:

* `WinsockGuard` 생성
* `ListenSocket` 생성
* `accept()` 루프 실행
* `ClientSocket` 생성
* `ClientSession` 생성
* `ClientManager` 등록
* 클라이언트별 thread 생성

#### Client.cpp

클라이언트 실행 코드입니다.

역할:

* 서버 연결
* 메시지 송신
* 메시지 수신

#### Common.h

공통 오류 처리 함수를 정의합니다.

역할:

* `err_quit()`
* `err_display()`

#### NetCommon.h

네트워크 프로토콜 및 송수신 helper를 정의합니다.

역할:

* `PacketHeader`
* `NetState`
* `send_all()`
* `recv_all()`
* 프로토콜 관련 상수

#### socketRAII.h

Winsock2 socket 자원을 RAII 방식으로 관리하는 객체들을 정의합니다.

역할:

* `WinsockGuard`
* `ListenSocket`
* `ClientSocket`
* `ConnectSocket`

---

## 5. 기존 프로토콜 구조

기존 Echo Server 프로젝트의 패킷 구조를 유지합니다.

```text
[PacketHeader][Payload]
```

### PacketHeader

```cpp
#pragma pack(push, 1)
struct PacketHeader {
    int32_t type;
    uint32_t length;
};
#pragma pack(pop)
```

### Header Field

#### type

패킷 타입을 나타냅니다.

현재는 일반 메시지 / 에러 메시지 구분에 사용하며,
추후 채팅 메시지 타입 확장에 사용할 수 있습니다.

#### length

payload 길이를 나타냅니다.

* 최대 payload 길이: 4096 bytes

### Payload

실제 메시지 데이터입니다.

* 최대 4096 bytes
* 문자열의 null 문자 `\0`는 송신하지 않음
* 수신 측에서 출력용 null 문자를 직접 추가

---

## 6. partial send / recv 처리

TCP는 message boundary를 보장하지 않는 byte stream 기반 프로토콜입니다.

따라서 한 번의 `send()`가 한 번의 `recv()`와 정확히 대응된다고 가정할 수 없습니다.

이를 해결하기 위해 기존 프로젝트의 helper 함수를 그대로 사용합니다.

* `send_all()`
* `recv_all()`

### send_all()

역할:

* 요청한 길이만큼 반복해서 `send()`
* partial send 발생 시 남은 바이트를 계속 송신
* `SOCKET_ERROR` 발생 시 즉시 반환

### recv_all()

역할:

* 요청한 길이만큼 반복해서 `recv()`
* partial recv 발생 시 남은 바이트를 계속 수신
* `SOCKET_ERROR` 발생 시 즉시 반환
* `recv() == 0`이면 peer graceful shutdown으로 판단

---

## 7. 상태 관리 구조체 (`NetState`)

기존 Echo Server 프로젝트에서 사용한 `NetState` 구조체를 유지합니다.

`NetState`는 통신 과정에서 현재 어떤 단계가 완료되었는지,
또 어떤 예외 상황이 발생했는지를 기록하기 위한 상태 관리 구조체입니다.

역할:

* header 송신 여부
* payload 송신 여부
* header 수신 여부
* payload 수신 여부
* transport error 발생 여부
* peer exit 여부
* protocol error 발생 여부

멀티스레드 구조에서는 각 클라이언트별 송수신 상태가 독립적이어야 합니다.

따라서 현재 설계에서는 `NetState`를 `ClientSession`의 멤버로 두고,
각 세션이 자기 자신의 송수신 상태를 값으로 소유하도록 구성했습니다.

```cpp
class ClientSession {
private:
    NetState ClientState;
};
```

`ClientSocket`이 `NetState&`를 멤버로 들고 있을 필요는 없습니다.

대신 송신 / 수신 시점에 `ClientSession`이 소유한 `ClientState`를
`ClientSockSend()` / `ClientSockRecv()`에 레퍼런스로 넘겨 상태 변화를 반영합니다.

```cpp
ClientSock->ClientSockSend(ClientState, msg, len);
ClientSock->ClientSockRecv(ClientState, buf, len);
```

---

## 8. 기존 OOP / RAII 기반 socket 관리 구조

기존 프로젝트에서 적용한 OOP / RAII 기반 socket 관리 구조를 유지합니다.

핵심 원칙은 다음과 같습니다.

```text
socket resource lifetime == object lifetime
```

즉, socket이라는 자원의 수명을 객체의 수명과 묶어
자원 누수와 중복 해제를 방지합니다.

### 8-1. WinsockGuard

Winsock 초기화와 정리를 담당하는 RAII 객체입니다.

역할:

* 생성자에서 `WSAStartup()` 호출
* 소멸자에서 `WSACleanup()` 호출

이를 통해 예외가 발생하더라도 Winsock 정리가 자동으로 수행되도록 합니다.

### 8-2. ListenSocket

서버의 listen socket을 관리하는 객체입니다.

역할:

* `socket()`
* `setsockopt(SO_REUSEADDR)`
* `bind()`
* `listen()`
* `accept()`

`ListenSocket`은 listen용 socket을 소유하며,
소멸 시 `closesocket()`을 통해 자원을 정리합니다.

또한 socket handle의 중복 소유를 막기 위해 복사를 금지합니다.

```cpp
ListenSocket(const ListenSocket&) = delete;
ListenSocket& operator=(const ListenSocket&) = delete;
```

### 8-3. ClientSocket

`accept()` 이후 생성되는 client socket을 관리하는 객체입니다.

역할:

* raw `SOCKET` 소유
* `send_all()` 기반 송신
* `recv_all()` 기반 수신
* 소멸 시 `closesocket()` 자동 호출

`ClientSocket`은 raw socket을 소유하는 RAII 객체이므로 복사를 금지합니다.

```cpp
ClientSocket(const ClientSocket&) = delete;
ClientSocket& operator=(const ClientSocket&) = delete;
```

반면, `accept()` 결과를 객체로 반환하거나
`ClientSession`으로 소유권을 넘기기 위해 이동은 허용합니다.

```cpp
ClientSocket(ClientSocket&& other) noexcept;
```

### 8-4. ConnectSocket

클라이언트 측 connect socket을 관리하는 객체입니다.

역할:

* `socket()`
* `connect()`
* `send_all()` 기반 송신
* `recv_all()` 기반 수신
* 소멸 시 `closesocket()` 자동 호출

클라이언트 프로그램에서 서버와의 연결을 담당합니다.

---

## 9. 전체 구조와 책임 분리

이번 멀티클라이언트 서버 설계에서 핵심은
**객체의 책임**과 **스레드의 책임**을 분리하는 것입니다.

단순히 기능을 클래스로 나누는 것이 아니라,

* 어떤 객체가 어떤 자원을 소유하는지
* 어떤 스레드가 어떤 객체를 사용하는지
* 어떤 객체가 세션의 생명주기를 관리하는지
* 어떤 mutex가 어떤 공유 자원을 보호하는지

를 명확히 나누는 것을 목표로 합니다.

### 9-1. main thread

`main thread`는 서버의 진입점이며, 새로운 클라이언트 연결을 받아들이는 역할을 담당합니다.

역할:

* `WinsockGuard` 생성
* `ListenSocket` 생성
* `accept()` 루프 실행
* 새 클라이언트 연결 수락
* `ClientSocket` 생성
* `ClientSession` 생성
* `ClientManager`에 세션 등록
* 클라이언트별 `client_thread` 생성
* 생성한 thread를 `detach()`

`main thread`는 각 클라이언트의 세부 송수신을 직접 처리하지 않습니다.

대신 새로운 연결을 받아 `ClientSession`으로 묶고,
해당 세션을 담당할 thread를 생성하는 역할에 집중합니다.

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

### 9-2. client_thread

`client_thread`는 특정 클라이언트 하나를 담당하는 실행 흐름입니다.

역할:

* 특정 `ClientSession` 하나를 인자로 받음
* `ClientSession::Run()` 실행
* 해당 클라이언트의 송 / 수신 루프 수행
* transport error / peer exit / protocol error 감지
* 종료 상황 발생 시 `closing = true` 설정
* 종료 전 `ClientManager`에 자기 세션 제거 요청

현재 Echo Server 단계에서는 수신한 메시지를 같은 클라이언트에게 다시 돌려보냅니다.

추후 Chat Server 단계에서는 수신한 메시지를
`ClientManager::Broadcast()`로 전달하는 구조로 확장할 예정입니다.

```text
client_thread
  └── session->Run()
          ├── header recv
          ├── payload recv
          ├── echo send
          │
          ├── error / peer_exit / protocol error 감지
          ├── closing = true
          ├── RemoveThisClient()
          └── return
```

### 9-3. ClientSession

`ClientSession`은 클라이언트 한 명의 연결 단위를 표현하는 객체입니다.

역할:

* 클라이언트 한 명의 `ClientSocket` 소유
* 클라이언트 주소 정보 저장
* 클라이언트별 `NetState` 저장
* 클라이언트별 논리적 종료 상태 `closing` 관리
* 해당 클라이언트의 송수신 루프 구현부
* 종료 시 `ClientManager`에 자기 자신 제거 요청
* 추후 해당 클라이언트에 대한 `SendPacket()` / `RecvPacket()` 제공

즉, `ClientSession`은
**클라이언트 한 명과 관련된 소켓, 상태, 송수신 흐름을 묶는 객체**입니다.

### 9-4. ClientManager

`ClientManager`는 현재 접속 중인 여러 `ClientSession`을 관리하는 객체입니다.

역할:

* 새 `ClientSession` 추가
* 종료된 `ClientSession` 제거
* 현재 접속 중인 클라이언트 목록 관리
* clients 컨테이너 동기화
* 추후 broadcast 수행

`ClientManager`는 개별 클라이언트의 세부 송수신 과정을 직접 처리하지 않습니다.

대신 여러 `ClientSession` 사이의 관계와 목록을 관리합니다.

---

## 10. 신규 설계 객체 및 함수

멀티클라이언트 / 멀티스레드 구조로 확장하기 위해 다음 요소를 새로 설계합니다.

* `ClientSession`
* `ClientManager`
* `client_thread()`

이 챕터에서는 기존 객체가 아닌, 이번 프로젝트에서 새로 추가되는 설계 요소만 정리합니다.

---

### 10-1. ClientSession

`ClientSession`은 클라이언트 한 명의 연결 단위를 표현하는 객체입니다.

현재 설계에서는 `ClientSession` 하나가 특정 클라이언트에 대한 대부분의 정보를 담당합니다.

즉, 클라이언트의 socket, 주소 정보, 송수신 상태, 종료 예정 상태, 송수신 루프를 하나의 세션 객체 안에서 관리합니다.

역할:

* 클라이언트 한 명의 연결 단위 표현
* 해당 클라이언트의 `ClientSocket` 소유
* 클라이언트 주소 정보 저장
* 클라이언트별 송수신 상태 저장
* 클라이언트별 논리적 종료 상태 저장
* 클라이언트별 송수신 루프 실제 구현
* 필요 시 자신을 관리하는 `ClientManager` 참조
* 추후 채팅 서버에서 닉네임 / 세션 ID / 접속 상태 등 관리 기능 추가

현재 구조:

```cpp
class ClientSession : public std::enable_shared_from_this<ClientSession> {
private:
    std::unique_ptr<ClientSocket> ClientSock;
    sockaddr_in ClientAddr;
    std::weak_ptr<ClientManager> Manager_wp;
    NetState ClientState;
    std::atomic<bool> closing = false;

public:
    ClientSession(std::unique_ptr<ClientSocket> s, sockaddr_in addr);

    void AddToManager(std::shared_ptr<ClientManager> manager_sp);
    void Run();
    void RemoveThisClient();

    bool IsClosing() const;
};
```

#### ClientSocket 소유

`ClientSession`은 해당 클라이언트와 통신하는 `ClientSocket`을 독점 소유합니다.

```text
ClientSession
  └── unique_ptr<ClientSocket>
```

`ClientSocket`은 실제 raw `SOCKET`을 소유하는 RAII 객체이며,
해당 socket은 특정 클라이언트 세션 하나에만 속하는 것이 자연스럽습니다.

따라서 현재 설계에서는 `unique_ptr<ClientSocket>`을 사용하여
`ClientSession`이 `ClientSocket`을 독점 소유하도록 구성했습니다.

```cpp
ClientSession::ClientSession(std::unique_ptr<ClientSocket> s, sockaddr_in addr)
    : ClientSock(std::move(s)),
      ClientAddr(addr),
      ClientState{},
      closing(false) {
}
```

생성자 매개변수로 들어온 `s`는 타입이 `unique_ptr<ClientSocket>`이라도,
함수 내부에서는 이름이 있는 변수이므로 l-value로 취급됩니다.

따라서 멤버인 `ClientSock`으로 소유권을 넘기려면
생성자 내부에서도 `std::move(s)`가 필요합니다.

#### ClientAddr

`ClientAddr`은 해당 클라이언트의 주소 정보를 저장합니다.

```cpp
sockaddr_in ClientAddr;
```

`SOCKET`처럼 닫아야 하는 자원이 아니라 단순 값 데이터에 가까우므로,
현재는 값 복사로 관리합니다.

추후 IPv6까지 고려한다면 `sockaddr_storage`로 확장할 수 있습니다.

#### ClientManager 참조

`ClientSession`은 자신을 관리하는 `ClientManager`에 접근해야 할 수 있습니다.

예:

* 접속 종료 시 자신을 목록에서 제거 요청
* 추후 채팅 메시지 broadcast 요청
* 서버 공용 상태 접근

하지만 `ClientSession`이 `ClientManager`를 소유해서는 안 됩니다.

따라서 `ClientSession`은 `ClientManager`를 `weak_ptr`로 참조합니다.

```text
ClientManager
  └── shared_ptr<ClientSession>

ClientSession
  └── weak_ptr<ClientManager>
```

이 구조를 통해 순환 참조를 방지합니다.

`ClientSession`은 `AddToManager()`를 통해 `ClientManager`의 `shared_ptr`을 전달받고,
이를 내부의 `weak_ptr<ClientManager>`에 저장합니다.

```cpp
void ClientSession::AddToManager(std::shared_ptr<ClientManager> manager_sp) {
    Manager_wp = manager_sp;
}
```

#### closing

`closing`은 `ClientSession`의 논리적 종료 상태를 나타냅니다.

```cpp
std::atomic<bool> closing = false;
```

의미:

```text
closing == false
  → 아직 정상 송수신 가능한 세션

closing == true
  → 객체는 아직 살아있지만, 더 이상 정상적인 송신 대상으로 보지 않는 종료 예정 세션
```

`detach()` 기반 구조에서는 `std::thread` 객체를 통해 client thread의 종료 여부를 직접 관측할 수 없습니다.

따라서 분리된 thread의 상태는 `std::thread` 객체가 아니라
`ClientSession` 객체 내부 상태로 기록합니다.

현재는 `Run()` 실행 중 `transport error`, `peer exit`, `protocol error` 등으로
세션을 종료해야 하는 상황이 발생하면 `closing`을 `true`로 바꿉니다.

이후 `ClientManager`가 broadcast를 수행할 때
`closing == true`인 세션에는 `SendPacket()`을 수행하지 않도록 설계할 예정입니다.

`closing`은 여러 스레드에서 읽고 쓸 수 있으므로 `std::atomic<bool>`을 사용합니다.

단일 bool 값의 원자적인 읽기 / 쓰기만 필요하고,
복잡한 상태 변경을 보호하는 상황은 아니기 때문에
현재 단계에서는 `std::mutex`보다 `std::atomic`이 더 적절하다고 판단했습니다.

#### Run()

`Run()`은 해당 클라이언트의 송 / 수신 루프를 담당합니다.

현재는 기존 단일 클라이언트 Echo Server의 송수신 로직을 기반으로,
`ClientSession` 내부에서 직접 송신 / 수신 과정을 수행하도록 구성했습니다.

기존 Echo Server와 큰 로직 변화는 없으며, 주요 변경점은 다음과 같습니다.

* raw socket 직접 사용 대신 `ClientSock` 사용
* 전역 또는 지역 상태 대신 `ClientState` 사용
* `ClientSockSend()` / `ClientSockRecv()` 호출 시 `ClientState`를 레퍼런스로 전달
* 클라이언트 한 개의 통신 흐름을 `ClientSession::Run()` 안에 통합
* 종료 상황 발생 시 `closing`을 `true`로 변경
* 종료 직전 `RemoveThisClient()`를 호출하여 `ClientManager`에 제거 요청

현재는 송신 / 수신 함수를 더 잘게 분리하지 않고,
에러 처리 흐름과 기존 Echo 로직을 유지하기 위해 `Run()` 안에 통합했습니다.

추후 구조가 안정되면 다음 함수들로 분리할 수 있습니다.

* `RecvPacket()`
* `SendPacket()`
* `SendHeaderErrorPacket()`

#### RemoveThisClient()

`RemoveThisClient()`는 현재 `ClientSession`이
자신을 관리하는 `ClientManager`에게 제거를 요청하는 함수입니다.

```cpp
void ClientSession::RemoveThisClient() {
    if (auto locked = Manager_wp.lock()) {
        locked->RemoveClient(shared_from_this());
    }
    else {
	std::cout << "ClientManager 객체 이미 소멸됨. RemoveClient()가 실행되지 않습니다.";
    }
}
```

`Manager_wp`는 `weak_ptr<ClientManager>`이므로,
실제로 `ClientManager`에 접근하기 전 `lock()`을 통해 아직 `ClientManager`가 살아있는지 확인합니다.

만약 `ClientManager`가 소멸되어있다면,
`ClientManager`에 접근하지 않고 에러 메시지를 출력합니다.

`shared_from_this()`를 사용하려면 `ClientSession`이
`std::enable_shared_from_this<ClientSession>`를 상속해야 하며,
`ClientSession` 객체가 반드시 `std::shared_ptr`로 관리되는 상태여야 합니다.

---

### 10-2. ClientManager

`ClientManager`는 접속 중인 클라이언트 세션들을 관리하는 객체입니다.

역할:

* 클라이언트 추가
* 클라이언트 제거
* 현재 접속 중인 클라이언트 목록 관리
* 추후 브로드캐스트 메시지 전송
* 공유 컨테이너 동기화

현재 구조:

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

#### ClientSession 관리

`ClientManager`는 여러 `ClientSession`을 관리해야 하므로
`std::vector<std::shared_ptr<ClientSession>>` 형태의 컨테이너를 사용합니다.

```text
ClientManager
  └── vector<shared_ptr<ClientSession>>
```

`ClientSession`은 `ClientManager`뿐만 아니라
각 클라이언트를 담당하는 thread에서도 사용됩니다.

따라서 `shared_ptr<ClientSession>`으로 세션 객체의 물리적 생존을 관리합니다.

#### AddClient()

`AddClient()`는 새로 생성된 `ClientSession`을 `clients` 컨테이너에 추가합니다.

```cpp
void ClientManager::AddClient(std::shared_ptr<ClientSession> client) {
    client->AddToManager(shared_from_this());

    std::lock_guard<std::mutex> lock(client_mutex);
    clients.push_back(client);
}
```

`clients`는 여러 thread가 접근할 수 있는 공유 컨테이너이므로
`std::lock_guard<std::mutex>`를 사용하여 접근 구간을 보호합니다.

현재 구조에서는 `ClientManager`가 `shared_ptr<ClientSession>`을 보관하고,
detached client thread도 `shared_ptr<ClientSession>`을 보관합니다.

따라서 둘 중 하나라도 세션 객체를 참조하고 있다면 `ClientSession`은 소멸하지 않습니다.

#### RemoveClient()

`RemoveClient()`는 특정 `ClientSession`을 `clients` 컨테이너에서 제거합니다.

```cpp
void ClientManager::RemoveClient(std::shared_ptr<ClientSession> client) {
    std::lock_guard<std::mutex> lock(client_mutex);

    clients.erase(
        std::remove(clients.begin(), clients.end(), client),
        clients.end()
    );
}
```

`RemoveClient()`는 현재 임시 구현 단계입니다.

추후 `ClientManager` 구조를 더 정리하면서
제거 정책과 broadcast 중 제거 처리 방식을 다시 다듬을 예정입니다.

중요한 점은 `RemoveClient()`가 곧바로 `ClientSession` 객체의 소멸을 의미하지 않는다는 것입니다.

`RemoveClient()`는 `ClientManager`의 관리 목록에서 해당 세션을 제외하는 동작입니다.

만약 detached client thread가 아직 `shared_ptr<ClientSession>`을 들고 있다면,
`ClientManager`에서 제거된 뒤에도 세션 객체는 계속 살아있습니다.

#### shared_from_this()

`ClientManager`가 자신을 관리하는 `shared_ptr`을 `ClientSession`에 전달해야 할 수 있습니다.

이때 `this`로부터 직접 `shared_ptr`을 만들면
기존 control block과 다른 control block이 생길 수 있어 위험합니다.

따라서 `ClientManager`는 `std::enable_shared_from_this<ClientManager>`를 상속하고,
`shared_from_this()`를 통해 기존 control block을 공유하는 `shared_ptr`을 얻도록 설계합니다.

```cpp
class ClientManager
    : public std::enable_shared_from_this<ClientManager> {
};
```

주의:

```cpp
auto manager = std::make_shared<ClientManager>();
```

처럼 `ClientManager` 객체가 반드시 `shared_ptr`로 관리되는 상태에서
`shared_from_this()`를 호출해야 합니다.

---

### 10-3. client_thread()

`client_thread()`는 각 클라이언트의 송수신 루프를 담당하는 thread entry function입니다.

현재 형태:

```cpp
void client_thread(std::shared_ptr<ClientSession> session) {
    session->Run();
}
```

`ClientSession`은 `ClientManager`와 `client_thread()` 양쪽에서 사용됩니다.

따라서 thread에는 `shared_ptr<ClientSession>`을 전달하여,
thread가 실행되는 동안 해당 세션 객체가 살아있도록 합니다.

```text
ClientManager
  └── shared_ptr<ClientSession>

client_thread()
  └── shared_ptr<ClientSession>
```

현재는 `std::thread` 생성 직후 `detach()`를 호출하는 구조를 사용합니다.

```cpp
auto session = std::make_shared<ClientSession>(std::move(client_socket), client_addr);
manager->AddClient(session);

std::thread ClientThread(client_thread, session);
ClientThread.detach();
```

`detach()`를 선택한 이유는 현재 단계에서 `join()` 기반으로
thread 종료 시점, `ClientSession` 제거 시점, `ClientManager`의 관리 책임을
모두 정확히 묶는 구조가 지나치게 복잡했기 때문입니다.

따라서 1차 구현에서는 다음 구조를 선택했습니다.

```text
std::thread 객체로 client thread의 종료를 직접 회수하지 않는다.
대신 ClientSession의 shared_ptr와 closing 상태값으로 세션 객체의 수명과 종료 상태를 관리한다.
```

`detach()`는 thread를 완전히 방치한다는 의미가 아니라,
`std::thread` 객체를 통한 회수를 포기하고
다른 수명 / 상태 모델로 관리한다는 의미에 가깝습니다.

---

## 11. 객체 소유 관계와 종료 상태

현재 객체 소유 관계는 다음과 같습니다.

```text
ClientManager
  └── shared_ptr<ClientSession>

client_thread()
  └── shared_ptr<ClientSession>

ClientSession
  ├── unique_ptr<ClientSocket>
  ├── weak_ptr<ClientManager>
  ├── NetState
  └── atomic<bool> closing

ClientSocket
  └── SOCKET
```

### 11-1. 물리적 생존

`ClientSession`의 물리적 생존은 `shared_ptr`의 reference count로 관리합니다.

```text
ClientManager가 clients에 보관 중이거나
OR
client_thread가 shared_ptr<ClientSession>을 들고 있으면

ClientSession 객체는 소멸하지 않는다.
```

따라서 `ClientManager`에서 해당 세션을 `RemoveClient()`로 제거하더라도,
detached client thread가 아직 `shared_ptr<ClientSession>`을 들고 있으면
세션 객체는 즉시 소멸하지 않습니다.

반대로 client thread가 먼저 종료되더라도
`ClientManager`가 아직 세션을 보관하고 있다면 세션 객체는 여전히 살아있습니다.

### 11-2. 논리적 종료 상태

`ClientSession`의 논리적 종료 상태는 `closing`으로 관리합니다.

```text
closing == false
  → 정상 송수신 대상

closing == true
  → 객체는 아직 살아있지만, 종료 예정 상태
```

즉, `shared_ptr`은 `ClientSession`의 물리적 생존을 보장하고,
`closing`은 `ClientSession`의 논리적 종료 상태를 나타냅니다.

### 11-3. RemoveClient의 의미

`RemoveClient()`는 세션 객체를 즉시 소멸시키는 함수가 아니라,
`ClientManager`의 관리 목록에서 해당 세션을 제외하는 함수입니다.

```text
RemoveClient()
  → clients 컨테이너에서 shared_ptr 제거
  → ClientManager의 관리 목록에서 제외
  → 마지막 shared_ptr이 사라진 경우에만 ClientSession 소멸
```

따라서 현재 구조에서는 다음 세 가지를 구분합니다.

```text
shared_ptr
  → 물리적 생존 보장

closing
  → 논리적 종료 상태

RemoveClient()
  → ClientManager의 관리 목록에서 제거
```

이 구분은 현재 설계에서 중요한 기준입니다.

객체가 아직 살아있다는 것과,
그 객체가 정상적인 송수신 대상으로 남아있다는 것은 서로 다른 문제입니다.

---

## 12. Echo Server에서 Chat Server로 확장 예정

현재 구현 단계에서는 각 클라이언트가 보낸 메시지를
해당 클라이언트에게 다시 돌려보내는 Echo Server 구조입니다.

```text
Client A → Server → Client A
```

추후 Chat Server 단계에서는 `ClientManager::Broadcast()`를 추가하여,
한 클라이언트가 보낸 메시지를 다른 클라이언트들에게 전달하도록 확장할 예정입니다.

```text
Client A → Server → Client B
                  → Client C
                  → Client D
```

### 12-1. Broadcast 설계 목표

브로드캐스트 기능을 추가할 때 고려해야 할 핵심은 다음과 같습니다.

* 현재 접속 중인 `ClientSession` 목록을 안전하게 순회
* `closing == true`인 세션은 송신 대상에서 제외
* 송신 중 실패한 클라이언트 처리
* sender 자신에게도 보낼 것인지 여부 결정
* `send()` 중 block될 수 있는 구간과 manager lock 범위 분리
* 같은 클라이언트에 대한 패킷 송신 순서 보장
* 추후 `SessionID`, nickname, message type 확장 고려

### 12-2. Broadcast 전체에 하나의 lock을 거는 방식

처음 생각할 수 있는 방식은 `Broadcast()` 전체에 하나의 lock을 거는 것입니다.

```text
Broadcast() 시작
  → clients lock
  → 모든 ClientSession에 send
  → clients unlock
```

하지만 이 방식은 문제가 있습니다.

* 한 클라이언트에게 `send()` 중일 때 다른 클라이언트 관련 작업까지 막힘
* 느린 클라이언트 하나가 전체 broadcast를 지연시킬 수 있음
* `send()`처럼 block될 수 있는 작업을 manager lock 안에서 수행하게 됨
* 전체 병렬성이 낮아짐

### 12-3. ClientManager lock과 ClientSession send lock 분리

현재 설계에서는 다음과 같이 lock의 역할을 분리할 예정입니다.

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

이를 위해 각 `ClientSession`마다 별도의 `send_mutex`를 둘 예정입니다.

```cpp
class ClientSession {
private:
    std::mutex send_mutex;

public:
    void SendPacket(const Packet& packet) {
        std::lock_guard<std::mutex> lock(send_mutex);

        send_all(packet.header);
        send_all(packet.payload);
    }
};
```

### 12-4. Broadcast 예상 흐름

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

예상 구조는 다음과 같습니다.

```cpp
void ClientManager::Broadcast(const Packet& packet) {
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

        client->SendPacket(packet);
    }
}
```

이 구조의 장점은 다음과 같습니다.

* `clients_mutex`는 clients 목록을 복사하는 짧은 구간에서만 사용
* 실제 `send()`는 `clients_mutex`를 잡지 않은 상태에서 수행
* 같은 `ClientSession`에 대한 송신만 `send_mutex`로 직렬화
* 서로 다른 `ClientSession`에 대한 송신은 서로의 `send_mutex`에 직접 영향을 주지 않음
* Header / Payload 패킷 경계가 섞이는 문제를 방지할 수 있음

다만 하나의 `Broadcast()` 호출 내부에서 `snapshot`을 단일 thread가 순회한다면,
그 `Broadcast()` 내부의 send 호출 자체는 순차적으로 진행됩니다.

즉, 현재 설계의 의미는 “모든 송신이 완전히 병렬로 수행된다”가 아니라,
**서로 다른 ClientSession에 대한 send lock이 서로를 직접 막지 않도록 락 범위를 세분화한다**는 것입니다.

현재 단계에서는 아직 `Broadcast()`, `SendPacket()`, `send_mutex`가 구현되지 않았습니다.

이 섹션은 추후 Chat Server 확장을 위한 동기화 설계 기록입니다.

---

## 13. 종료 흐름

전체 종료 흐름은 다음과 같습니다.

```text
1. client_thread가 transport error / peer exit / protocol error를 감지한다.
2. ClientSession::closing을 true로 변경한다.
3. 이후 ClientManager의 broadcast는 해당 세션을 send 대상에서 제외한다.
4. client_thread는 Run() 종료 전에 RemoveThisClient()를 호출한다.
5. RemoveThisClient()는 Manager_wp.lock()으로 ClientManager에 접근한다.
6. ClientManager::RemoveClient()가 clients에서 해당 shared_ptr을 제거한다.
7. Run()이 return되면 client_thread가 들고 있던 shared_ptr도 해제된다.
8. 마지막 shared_ptr이 사라지면 ClientSession이 소멸한다.
9. ClientSession이 소유하던 ClientSocket도 소멸하면서 raw SOCKET이 closesocket()으로 정리된다.
```

이 구조에서 중요한 점은 `RemoveClient()`와 객체 소멸이 같은 의미가 아니라는 것입니다.

`RemoveClient()`는 `ClientManager`의 관리 목록에서 해당 세션을 제외하는 동작이고,
실제 객체 소멸은 마지막 `shared_ptr`이 사라지는 시점에 발생합니다.

---

## 14. 현재 구현 / 설계 상태

### 구현 완료

* 기존 단일 클라이언트 Echo Server 구현 완료
* 기존 Echo Server의 OOP / RAII 리팩토링 완료
* `WinsockGuard` 설계 및 구현
* `ListenSocket` 설계 및 구현
* `ClientSocket` 설계 및 구현
* `ConnectSocket` 설계 및 구현
* `ClientManager` 기본 구조 구현
* `ClientSession` 기본 구조 구현
* `ClientSession` 내부에 `ClientSocket`, `ClientAddr`, `Manager_wp`, `ClientState`, `closing` 배치
* `ClientSession` 생성자에서 `unique_ptr<ClientSocket>` 소유권 이동 처리
* `std::make_shared<ClientSession>()`를 통한 세션 객체 생성 구조 적용
* `ClientSession::Run()`에 기존 Echo Server 송수신 루프 이식
* `client_thread(std::shared_ptr<ClientSession>)` 구조 적용
* `std::thread` 생성 직후 `detach()`하는 구조 적용
* detached thread가 `shared_ptr<ClientSession>`을 들고 `Run()`을 실행하는 구조 적용
* `std::atomic<bool> closing`을 통한 논리적 종료 상태 관리
* `ClientSession::RemoveThisClient()` 구조 추가
* `ClientManager::RemoveClient(std::shared_ptr<ClientSession>)` 임시 구현

### 설계 완료 / 구현 예정

* `ClientSession::SendPacket()`
* ClientSession별 `send_mutex`
* `ClientManager::Broadcast()`
* broadcast 시 clients snapshot 복사 구조
* `closing == true`인 세션 송신 제외 처리
* 송신 실패한 클라이언트 처리 정책
* `SessionID`
* nickname
* message type 확장
* broadcast 기반 Chat Server 단계

### 현재 기준 미구현

현재 기준으로 다음 기능은 아직 구현되지 않았습니다.

* Broadcast Chat 기능
* `SessionID`
* nickname 기반 클라이언트 식별
* 메시지 타입별 채팅 프로토콜 분기
* ClientSession별 send mutex 적용
* broadcast 중 송신 실패 세션 정리 정책

---

## 15. 사용 환경

* Windows
* C++
* Winsock2
* Visual Studio

---

## 16. 실행 방법

현재 프로젝트는 기존 Echo Server를 멀티클라이언트 / 멀티스레드 구조로 확장하는 단계입니다.

구현이 안정화되면 다음 방식으로 실행할 예정입니다.

### 16-1. 서버 실행

```text
Server.cpp 실행
```

### 16-2. 클라이언트 실행

```text
Client.cpp 실행
```

### 16-3. 여러 클라이언트 접속

여러 개의 클라이언트를 실행하여 서버에 접속합니다.

### 16-4. Echo 단계

각 클라이언트는 자신이 보낸 메시지를 서버로부터 다시 수신합니다.

### 16-5. Chat 단계

브로드캐스트 기능이 추가되면,
한 클라이언트가 보낸 메시지를 다른 클라이언트들이 수신합니다.

---

## 17. 향후 개선 예정

### 17-1. ClientSession 구조 개선

* `ClientSession` 내부 송수신 로직 분리

  * `RecvPacket()`
  * `SendPacket()`
  * `SendHeaderErrorPacket()`
* `ClientSession`의 `closing` 상태 접근 함수 정리

  * `IsClosing()`
  * `MarkClosing()`
* `ClientSession`의 `shared_from_this()` 사용 구조 검증
* ClientSession별 `send_mutex` 추가
* 같은 클라이언트에 대한 Header / Payload 송신 순서 보장

### 17-2. ClientManager 구조 개선

* `ClientManager::RemoveClient()` 정식 구현
* `ClientManager::Broadcast()` 구현
* broadcast 시 snapshot 복사 구조 구현
* `closing == true`인 세션 송신 제외 처리
* mutex 기반 `clients` 컨테이너 동기화 구조 검증
* `ClientManager`의 lock과 `ClientSession`의 send lock 역할 분리
* broadcast 중 `clients_mutex`를 오래 잡지 않도록 구조 개선

### 17-3. 멀티스레딩 / 동기화 개선

* `std::lock_guard` / `std::unique_lock` 적용 범위 정리
* `send()` 중 blocking될 수 있는 구간과 lock 범위 분리
* 느린 클라이언트가 전체 broadcast에 미치는 영향 확인
* 접속 종료된 클라이언트 정리 정책 개선
* detach 기반 구조의 종료 정책 추가 검증
* 추후 ClientSession별 send queue 구조 검토

### 17-4. Chat Server 확장

* Broadcast Chat 단계 구현
* 추후 message type 확장
* 추후 nickname 추가
* 추후 `SessionID` 추가
* sender 자신에게도 broadcast할지 여부 정책 결정
* 송신 실패한 클라이언트 처리 정책 결정

### 17-5. 기타 개선

* `ClientSocket` move assignment 정리
* 객체 책임과 스레드 책임을 README에 지속적으로 반영
* `main thread`, `client_thread`, `ClientSession`, `ClientManager`의 책임 경계 검증

---

## 18. 정리

현재 프로젝트는 단일 클라이언트 Echo Server에서 출발하여,
멀티클라이언트 / 멀티스레드 Echo Server의 기본 구조를 만드는 단계입니다.

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

또한 현재 구조에서는 다음 세 가지를 구분합니다.

```text
shared_ptr
  → 객체의 물리적 생존 보장

closing
  → 세션의 논리적 종료 상태

RemoveClient()
  → ClientManager의 관리 목록에서 제거
```

추후 Chat Server 단계에서는 `ClientManager::Broadcast()`를 추가하고,
`ClientManager`의 clients lock과 `ClientSession`의 send lock을 분리하여
브로드캐스트 중 발생할 수 있는 동기화 문제를 다룰 예정입니다.
