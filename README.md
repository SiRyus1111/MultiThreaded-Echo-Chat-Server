# MultiThreaded Echo-Chat Server (Winsock2, C++)

기존 단일 클라이언트 Echo Server를 기반으로,  
멀티클라이언트 / 멀티스레드 Echo Server로 확장한 뒤,  
최종적으로 브로드캐스트 기반 Chat Server까지 발전시키는 프로젝트입니다.

> 이 프로젝트는 단순히 여러 클라이언트를 동시에 처리하는 데서 끝나지 않고,  
> 클라이언트 세션 관리, socket lifetime 관리, 객체 소유권 설계,  
> thread-per-client 구조, 공유 컨테이너 동기화, 브로드캐스트 메시지 흐름까지 고려하는 것을 목표로 합니다.

> 기존 프로젝트에서 구현한 TCP byte stream 처리, partial send/recv 대응,  
> `PacketHeader` 기반 애플리케이션 레벨 프로토콜, OOP / RAII 기반 socket 관리 구조를 바탕으로  
> 멀티클라이언트 서버 구조로 확장합니다.

---

## 1. 기반 프로젝트

이 프로젝트는 기존 Echo Server 프로젝트를 기반으로 확장합니다.

- 기존 프로젝트: [My First Echo Server Project](https://github.com/SiRyus1111/My-First-Echo-Server-Project)

기존 프로젝트에서는 다음 내용을 구현했습니다.

- TCP 기반 Echo Server / Client
- TCP byte stream 특성을 고려한 partial send / recv 처리
- `send_all()` / `recv_all()` helper 함수
- `PacketHeader(type, length)` 기반 메시지 구조
- protocol error / transport error / peer exit 구분 처리
- `NetState` 기반 통신 상태 관리
- OOP / RAII 기반 socket 자원 관리 구조

본 프로젝트는 위 구조를 기반으로,  
단일 클라이언트 Echo Server를 멀티클라이언트 / 멀티스레드 환경으로 확장합니다.

---

## 2. 프로젝트 목표

이 프로젝트의 핵심 목표는 다음과 같습니다.

- 기존 단일 클라이언트 Echo Server 로직을 멀티클라이언트 / 멀티스레드 구조로 확장
- 클라이언트마다 독립적인 thread를 부여하는 thread-per-client 구조 구현
- `ClientSession` 객체를 통해 클라이언트 단위의 상태와 통신 흐름 관리
- `ClientManager` 객체를 통해 접속 중인 클라이언트 목록 관리
- 공유 컨테이너에 대한 mutex 기반 동기화 적용
- 기존 `PacketHeader` 기반 프로토콜 유지 및 확장
- Echo Server 단계 이후 브로드캐스트 Chat Server로 확장
- OOP / RAII 기반 socket lifetime 관리 유지
- 스마트 포인터를 통한 객체 소유권 명확화

---

## 3. 개발 단계

본 프로젝트는 다음 흐름으로 확장할 예정입니다.

```text
기존 단일 클라이언트 Echo Server
        ↓
멀티클라이언트 / 멀티스레드 Echo Server
        ↓
브로드캐스트 Chat Server
````

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

* `Server.cpp`

  * 서버 실행 코드
  * `ListenSocket` 생성
  * `accept()` 루프
  * `ClientSession` 생성
  * `ClientManager` 등록
  * 클라이언트별 thread 생성 예정

* `Client.cpp`

  * 클라이언트 실행 코드
  * 서버 연결
  * 메시지 송수신

* `Common.h`

  * 공통 오류 처리 함수
  * `err_quit()`
  * `err_display()`

* `NetCommon.h`

  * `PacketHeader`
  * `NetState`
  * `send_all()`
  * `recv_all()`

* `socketRAII.h`

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

* `type`

  * 패킷 타입
  * 일반 메시지 / 에러 메시지 / 추후 채팅 메시지 타입 확장 가능

* `length`

  * payload 길이
  * 최대 4096 bytes

### Payload

* 실제 메시지 데이터
* 최대 4096 bytes
* 문자열의 null 문자 `'\0'`는 송신하지 않음
* 수신 측에서 출력용 null 문자를 직접 추가

---

## 6. partial send / recv 처리

TCP는 message boundary를 보장하지 않는 byte stream 기반 프로토콜입니다.

따라서 한 번의 `send()`가 한 번의 `recv()`와 정확히 대응된다고 가정할 수 없습니다.

이를 해결하기 위해 기존 프로젝트의 helper 함수를 그대로 사용합니다.

* `send_all()`
* `recv_all()`

### send_all()

* 요청한 길이만큼 반복해서 `send()`
* partial send 발생 시 남은 바이트를 계속 송신
* `SOCKET_ERROR` 발생 시 즉시 반환

### recv_all()

* 요청한 길이만큼 반복해서 `recv()`
* partial recv 발생 시 남은 바이트를 계속 수신
* `SOCKET_ERROR` 발생 시 즉시 반환
* `recv() == 0`이면 peer graceful shutdown으로 판단

---

## 7. 상태 관리 구조체 (`NetState`)

기존 Echo Server 프로젝트에서 사용한 `NetState` 구조체를 유지합니다.

`NetState`는 통신 과정에서 현재 어떤 단계가 완료되었는지,
또 어떤 예외 상황이 발생했는지를 기록하기 위한 상태 관리 구조체입니다.

예상 역할:

* header 송신 여부
* payload 송신 여부
* header 수신 여부
* payload 수신 여부
* transport error 발생 여부
* peer exit 여부
* protocol error 여부

멀티스레드 구조에서는 각 클라이언트별 송수신 상태가 독립적이어야 하므로,
`NetState`는 각 `ClientSession` 또는 `client_thread()` 단위로 관리할 예정입니다.

---

## 8. 기존 OOP / RAII 기반 socket 관리 구조

기존 프로젝트에서 적용한 OOP / RAII 기반 socket 관리 구조를 유지합니다.

핵심 원칙은 다음과 같습니다.

```text
socket resource lifetime == object lifetime
```

즉, socket이라는 자원의 수명을 객체의 수명과 묶어
자원 누수와 중복 해제를 방지합니다.

---

### 8-1. WinsockGuard

Winsock 초기화와 정리를 담당하는 RAII 객체입니다.

역할:

* 생성자

  * `WSAStartup()`

* 소멸자

  * `WSACleanup()`

이를 통해 예외가 발생하더라도 Winsock 정리가 자동으로 수행되도록 합니다.

---

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

---

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
`ClientSession`으로 소유권을 넘기기 위해 이동은 허용할 예정입니다.

```cpp
ClientSocket(ClientSocket&& other) noexcept;
ClientSocket& operator=(ClientSocket&& other) noexcept;
```

---

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

## 9. 신규 설계 객체 및 함수

멀티클라이언트 / 멀티스레드 구조로 확장하기 위해 다음 요소를 새로 설계합니다.

* `ClientSession`
* `ClientManager`
* `client_thread()`

이 챕터에서는 기존 객체가 아닌,
이번 프로젝트에서 새로 추가되는 설계 요소만 정리합니다.

---

### 9-1. ClientSession

`ClientSession`은 클라이언트 한 명의 세션 정보를 표현하는 객체입니다.

예상 역할:

* 클라이언트 한 명의 socket 관리
* 클라이언트 주소 정보 저장
* 송수신 상태 관리
* 클라이언트별 송수신 루프에서 사용
* 추후 채팅 서버에서 닉네임 / 세션 ID / 접속 상태 등 관리 가능

예상 멤버:

```cpp
class ClientSession {
private:
    std::unique_ptr<ClientSocket> sock;
    sockaddr_in addr;
    std::weak_ptr<ClientManager> manager_wp;
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
`ClientSession`이 `ClientSocket`을 독점 소유하도록 설계합니다.

추후 `ClientSocket`이 move-only 객체로 안정적으로 정리되면,
`unique_ptr<ClientSocket>` 대신 값 멤버로 직접 소유하는 구조도 고려할 수 있습니다.

#### ClientManager 참조

`ClientSession`은 자신을 관리하는 `ClientManager`에 접근해야 할 수 있습니다.

예:

* 접속 종료 시 자신을 목록에서 제거 요청
* 채팅 메시지 broadcast 요청
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

---

### 9-2. ClientManager

`ClientManager`는 접속 중인 클라이언트 세션들을 관리하는 객체입니다.

예상 역할:

* 클라이언트 추가
* 클라이언트 제거
* 현재 접속 중인 클라이언트 목록 관리
* 브로드캐스트 메시지 전송
* 공유 컨테이너 동기화

예상 멤버:

```cpp
class ClientManager : public std::enable_shared_from_this<ClientManager> {
private:
    std::vector<std::shared_ptr<ClientSession>> clients;
    std::mutex client_mutex;
};
```

#### ClientSession 관리

`ClientManager`는 여러 `ClientSession`을 관리해야 하므로
`std::vector<std::shared_ptr<ClientSession>>` 형태의 컨테이너를 사용할 예정입니다.

```text
ClientManager
  └── vector<shared_ptr<ClientSession>>
```

`ClientSession`은 `ClientManager`뿐만 아니라
각 클라이언트를 담당하는 thread에서도 사용될 수 있으므로
`shared_ptr<ClientSession>`으로 생명주기를 관리합니다.

#### mutex 기반 동기화

멀티스레드 환경에서는 여러 thread가 동시에 `clients` 컨테이너에 접근할 수 있습니다.

예:

* 새로운 클라이언트 접속
* 기존 클라이언트 종료
* 브로드캐스트 시 전체 클라이언트 순회
* 연결이 끊긴 클라이언트 제거

따라서 `clients`에 접근하는 구간은 mutex로 보호해야 합니다.

예상 구조:

```cpp
void ClientManager::AddClient(std::shared_ptr<ClientSession> client) {
    std::lock_guard<std::mutex> lock(client_mutex);
    clients.push_back(std::move(client));
}
```

현재는 동기화 라이브러리를 학습한 뒤,
`std::mutex`, `std::lock_guard`, `std::unique_lock` 등을 적용할 예정입니다.

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

### 9-3. client_thread()

`client_thread()`는 각 클라이언트의 송수신 루프를 담당하는 함수입니다.

예상 역할:

* `shared_ptr<ClientSession>` 인자로 세션 객체 수명 보장
* 해당 클라이언트의 메시지 수신
* Echo 단계에서는 받은 메시지를 그대로 다시 송신
* Chat 단계에서는 받은 메시지를 `ClientManager`를 통해 broadcast
* peer exit 감지
* transport error 감지
* protocol error 감지
* 종료 시 `ClientManager`에서 해당 세션 제거

예상 형태:

```cpp
void client_thread(std::shared_ptr<ClientSession> session) {
    // session->Run();
}
```

`ClientSession`은 `ClientManager`와 `client_thread()` 양쪽에서 사용되므로
thread에는 `shared_ptr<ClientSession>`을 전달하는 방향으로 설계합니다.

---

## 10. 객체 소유 관계

현재 예상하는 객체 소유 관계는 다음과 같습니다.

```text
ClientManager
  └── shared_ptr<ClientSession>

client_thread()
  └── shared_ptr<ClientSession>

ClientSession
  └── unique_ptr<ClientSocket>

ClientSocket
  └── SOCKET
```

### 의미

* `ClientManager`

  * 여러 `ClientSession`을 관리합니다.
  * 세션 목록을 유지해야 하므로 `shared_ptr<ClientSession>`을 가집니다.

* `client_thread()`

  * 특정 클라이언트의 송수신 루프를 담당합니다.
  * thread가 실행되는 동안 세션 객체가 살아있어야 하므로 `shared_ptr<ClientSession>`을 가집니다.

* `ClientSession`

  * 특정 클라이언트의 socket을 독점 소유합니다.
  * 내부에 `unique_ptr<ClientSocket>`을 가집니다.

* `ClientSocket`

  * raw `SOCKET`을 소유합니다.
  * 소멸 시 `closesocket()`을 호출합니다.

---

## 11. Echo Server에서 Chat Server로 확장

초기 단계에서는 기존 Echo Server와 동일하게,
클라이언트가 보낸 메시지를 해당 클라이언트에게 그대로 돌려보냅니다.

```text
Client A → Server → Client A
```

이후 Chat Server 단계에서는,
한 클라이언트가 보낸 메시지를 다른 클라이언트들에게 전달합니다.

```text
Client A → Server → Client B
                  → Client C
                  → Client D
```

이를 위해 `ClientManager`에 broadcast 기능을 추가할 예정입니다.

예상 기능:

```cpp
void ClientManager::Broadcast(
    const std::shared_ptr<ClientSession>& sender,
    const char* msg,
    int len
);
```

브로드캐스트 시 고려할 점:

* sender 자신에게도 보낼 것인지 여부
* 연결이 끊긴 클라이언트 제거
* 송신 중 실패한 클라이언트 처리
* `clients` 컨테이너 순회 중 동기화
* broadcast 중 lock 범위 최소화
* 추후 메시지 타입 확장

---

## 12. 현재 구현 / 설계 상태

현재까지의 상태는 다음과 같습니다.

* 기존 단일 클라이언트 Echo Server 구현 완료
* 기존 Echo Server의 OOP / RAII 리팩토링 완료
* `WinsockGuard` 설계 완료
* `ListenSocket` 설계 완료
* `ClientSocket` 설계 완료
* `ConnectSocket` 설계 완료
* `shared_ptr` / `weak_ptr` 기반 세션 관리 구조 학습 중
* `unique_ptr` 기반 독점 소유 구조 학습 중
* `ClientManager` 설계 초안 작성 중
* `ClientSession` 설계 초안 작성 중
* `client_thread()` 구조 설계 예정
* 멀티스레딩 / 동기화 라이브러리 학습 예정

---

## 13. 사용 환경

* Windows
* C++
* Winsock2
* Visual Studio

---

## 14. 실행 방법

현재 프로젝트는 기존 Echo Server를 멀티클라이언트 / 멀티스레드 구조로 확장하는 단계입니다.

구현이 안정화되면 다음 방식으로 실행할 예정입니다.

### 1. 서버 실행

```text
Server.cpp 실행
```

### 2. 클라이언트 실행

```text
Client.cpp 실행
```

### 3. 여러 클라이언트 접속

여러 개의 클라이언트를 실행하여 서버에 접속합니다.

### 4. Echo 단계

각 클라이언트는 자신이 보낸 메시지를 서버로부터 다시 수신합니다.

### 5. Chat 단계

브로드캐스트 기능이 추가되면,
한 클라이언트가 보낸 메시지를 다른 클라이언트들이 수신합니다.

---

## 15. 향후 개선 예정

* `unique_ptr` 학습 후 `ClientSession` 내부 소유 구조 정리
* `ClientSocket` move constructor / move assignment 정리
* `ClientSession` 생성 및 thread 전달 구조 정리
* `ClientManager::AddClient()` 구현
* `ClientManager::RemoveClient()` 구현
* `ClientManager::Broadcast()` 구현
* mutex 기반 `clients` 컨테이너 동기화
* `std::lock_guard` / `std::unique_lock` 적용
* 접속 종료된 클라이언트 정리
* 멀티클라이언트 / 멀티스레드 Echo 단계 안정화
* Broadcast Chat 단계 구현
* 추후 message type 확장
* 추후 nickname / session id 추가

```

그리고 하나만 더: 프로젝트 이름이 의도한 표기가 아니면 `MultiThreaded`는 `Multithreaded`로 바꾸는 게 자연스러워. 이미 레포명을 저렇게 잡았으면 그대로 둬도 됨.
```
