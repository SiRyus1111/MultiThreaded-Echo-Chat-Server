# Socket RAII

이 문서는 기존 Echo Server 프로젝트에서 가져온
OOP / RAII 기반 Winsock2 socket 관리 구조를 정리합니다.

핵심 원칙은 다음과 같습니다.

```text
socket resource lifetime == object lifetime
```

즉, socket이라는 자원의 수명을 객체의 수명과 묶어
자원 누수와 중복 해제를 방지합니다.

---

## 1. RAII를 적용하는 이유

Winsock2의 raw `SOCKET`은 직접 정리해야 하는 자원입니다.

즉, 더 이상 사용하지 않을 때 `closesocket()`을 호출해야 합니다.

하지만 프로그램이 복잡해질수록 다음 문제가 발생할 수 있습니다.

- 특정 분기에서 `closesocket()`을 빼먹음
- 예외나 조기 return으로 정리 코드에 도달하지 못함
- 같은 socket handle을 여러 객체가 소유해서 중복 해제함
- socket의 소유자가 누구인지 불명확해짐

이를 막기 위해 socket handle을 RAII 객체로 감쌉니다.

```text
객체 생성
  → socket 자원 획득

객체 소멸
  → socket 자원 정리
```

---

## 2. WinsockGuard

`WinsockGuard`는 Winsock 초기화와 정리를 담당하는 RAII 객체입니다.

### 역할

- 생성자에서 `WSAStartup()` 호출
- 소멸자에서 `WSACleanup()` 호출

개념적 구조:

```cpp
class WinsockGuard {
public:
    WinsockGuard() {
        WSAStartup(...);
    }

    ~WinsockGuard() {
        WSACleanup();
    }
};
```

`WinsockGuard`를 사용하면 예외가 발생하거나 함수가 중간에 return되더라도
객체 소멸 시점에 Winsock 정리가 자동으로 수행됩니다.

---

## 3. ListenSocket

`ListenSocket`은 서버의 listen socket을 관리하는 객체입니다.

### 역할

- `socket()`
- `setsockopt(SO_REUSEADDR)`
- `bind()`
- `listen()`
- `accept()`
- 소멸 시 `closesocket()`

`ListenSocket`은 listen용 socket을 소유합니다.

따라서 socket handle의 중복 소유를 막기 위해 복사를 금지합니다.

```cpp
ListenSocket(const ListenSocket&) = delete;
ListenSocket& operator=(const ListenSocket&) = delete;
```

### 설계 의미

```text
ListenSocket
  → 서버가 새 연결을 받기 위한 listen socket을 소유하는 객체
```

`accept()`의 결과로 나온 client socket은
`ClientSocket`으로 감싸서 별도로 관리합니다.

---

## 4. ClientSocket

`ClientSocket`은 `accept()` 이후 생성되는 client socket을 관리하는 객체입니다.

### 역할

- raw `SOCKET` 소유
- `send_all()` 기반 송신
- `recv_all()` 기반 수신
- 소멸 시 `closesocket()` 자동 호출

`ClientSocket`은 특정 클라이언트와 연결된 raw socket을 소유하는 RAII 객체입니다.

따라서 복사를 금지합니다.

```cpp
ClientSocket(const ClientSocket&) = delete;
ClientSocket& operator=(const ClientSocket&) = delete;
```

반면, `accept()` 결과를 객체로 반환하거나
`ClientSession`으로 소유권을 넘기기 위해 이동은 허용합니다.

```cpp
ClientSocket(ClientSocket&& other) noexcept;
ClientSocket& operator=(ClientSocket&& other) noexcept;
```

### ClientSession과의 관계

현재 설계에서는 `ClientSession`이 `ClientSocket`을 독점 소유합니다.

```text
ClientSession
  └── unique_ptr<ClientSocket>
        └── SOCKET
```

이 구조의 의미는 다음과 같습니다.

```text
특정 클라이언트와 연결된 socket은
해당 ClientSession 하나에 속한다.
```

---

## 5. ConnectSocket

`ConnectSocket`은 클라이언트 측 connect socket을 관리하는 객체입니다.

### 역할

- `socket()`
- `connect()`
- `send_all()` 기반 송신
- `recv_all()` 기반 수신
- 소멸 시 `closesocket()` 자동 호출

서버 측의 `ListenSocket` / `ClientSocket`과 달리,
`ConnectSocket`은 클라이언트 프로그램에서 서버와의 연결을 담당합니다.

```text
Client.cpp
  → ConnectSocket 생성
  → 서버 connect
  → 메시지 송신 / 수신
  → 객체 소멸 시 closesocket()
```

---

## 6. 복사 금지

socket handle은 단순 정수처럼 보이지만,
실제로는 운영체제 자원에 대한 handle입니다.

따라서 같은 raw socket handle을 여러 객체가 동시에 소유하면 문제가 생깁니다.

예를 들어 다음 상황은 위험합니다.

```text
ClientSocket A가 SOCKET 100을 소유
ClientSocket B도 SOCKET 100을 소유

A 소멸 → closesocket(100)
B 소멸 → closesocket(100) 다시 호출
```

이를 막기 위해 socket RAII 객체는 복사를 금지합니다.

```cpp
ClientSocket(const ClientSocket&) = delete;
ClientSocket& operator=(const ClientSocket&) = delete;
```

---

## 7. 이동 허용

복사는 금지하지만, 소유권 이동은 필요합니다.

예를 들어 `accept()` 결과로 생성한 `ClientSocket`을
`ClientSession`에게 넘겨야 합니다.

이때는 복사가 아니라 이동을 사용합니다.

```cpp
auto client_socket = std::make_unique<ClientSocket>(accepted_socket);
auto session = std::make_shared<ClientSession>(std::move(client_socket), client_addr);
```

`std::move()` 이후 원래 `unique_ptr`은 더 이상 socket을 소유하지 않습니다.

이 구조는 socket 소유자가 명확해진다는 장점이 있습니다.

---

## 8. 정리

이 프로젝트의 socket 관리 구조는 다음 원칙을 따릅니다.

```text
1. raw SOCKET은 직접 흩뿌리지 않는다.
2. socket은 RAII 객체가 소유한다.
3. socket RAII 객체는 복사 금지한다.
4. 필요한 경우에만 이동을 허용한다.
5. ClientSession은 ClientSocket을 unique_ptr로 독점 소유한다.
```

결국 목표는 다음입니다.

```text
socket 자원의 수명을 객체의 수명과 묶어
자원 누수, 중복 해제, 소유권 혼란을 줄인다.
```
