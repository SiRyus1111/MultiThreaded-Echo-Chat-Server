# Protocol

이 문서는 `MultiThreaded Echo Server`에서 사용하는 애플리케이션 레벨 프로토콜과
TCP byte stream 처리 방식을 정리합니다.

현재 프로토콜은 기존 단일 클라이언트 Echo Server에서 사용한 구조를 유지하며,
추후 Chat Server 단계에서 message type을 확장할 수 있도록 설계합니다.

---

## 1. TCP byte stream 문제

TCP는 message boundary를 보장하지 않는 byte stream 기반 프로토콜입니다.

따라서 다음과 같은 가정을 하면 안 됩니다.

```text
한 번의 send() == 한 번의 recv()
```

실제로는 다음과 같은 상황이 발생할 수 있습니다.

```text
송신 측:
send("HelloWorld", 10)

수신 측:
recv() → "Hel"
recv() → "loWo"
recv() → "rld"
```

또는 반대로 여러 번 보낸 데이터가 한 번에 붙어서 도착할 수도 있습니다.

따라서 애플리케이션 레벨에서 메시지의 경계를 직접 정의해야 합니다.

이 프로젝트에서는 다음 구조를 사용합니다.

```text
[PacketHeader][Payload]
```

---

## 2. PacketHeader

현재 패킷 헤더 구조는 다음과 같습니다.

```cpp
#pragma pack(push, 1)
struct PacketHeader {
    int32_t type;
    uint32_t length;
};
#pragma pack(pop)
```

### type

`type`은 패킷 타입을 나타냅니다.

현재는 일반 메시지와 에러 메시지, 닉네임 변경 메시지 구분을 위해 사용합니다.

현재 코드에서는 패킷 타입의 의미를 표현하기 위해 `PacketType` enum class를 사용합니다.

```cpp
enum class PacketType : int32_t {
    CHAT_MESSAGE = 1,
    NICKNAME_CHANGE = 2,
    HEADER_ERROR = 3
};
```

단, `PacketHeader::type` 자체는 `PacketType`이 아니라 `int32_t`로 유지합니다.

```text
PacketHeader::type
  → 실제 네트워크 패킷에 들어가는 정수 필드 = 값인 int32_t 형으로 읽히는게 좋다.

PacketType
  → 코드 내부에서 패킷 타입의 의미를 표현하는 enum class - 의미인 enum class로 읽히는게 좋다.
```

송신 시에는 `PacketType`을 `int32_t`로 변환한 뒤 `htonl()`을 적용합니다.

```cpp
send_net_header.type = htonl(static_cast<int32_t>(type));
```

수신 시에는 네트워크 바이트 오더의 `type`을 `ntohl()`로 변환한 뒤,
허용된 `PacketType` 값과 비교합니다.

```cpp
recv_host_header.type = ntohl(recv_net_header.type);

if (recv_host_header.type != static_cast<int32_t>(PacketType::CHAT_MESSAGE) &&
    recv_host_header.type != static_cast<int32_t>(PacketType::NICKNAME_CHANGE) &&
    recv_host_header.type != static_cast<int32_t>(PacketType::HEADER_ERROR)) {
    // protocol error
}
```

현재 타입:

```text
CHAT_MESSAGE
  → 일반 메시지

NICKNAME_CHANGE
  → 닉네임 변경 메시지

HEADER_ERROR
  → 프로토콜 에러 메시지
```

### Protocol Error 처리 흐름

현재 구조에서는 Header 검증 실패 시 즉시 연결을 종료하지 않습니다.

예를 들어

- payload length가 0인 경우
- payload length가 허용 범위(4096)를 초과한 경우
- 허용되지 않은 PacketType을 받은 경우

에는 protocol error로 판단합니다.

처리 흐름은 다음과 같습니다.

```text
잘못된 Header 수신
↓
protocol_error = true
↓
HEADER_ERROR 패킷 송신
↓
송신 결과 NetState 획득
↓
HandleTransportException(header_err_send_res)
```

에러 패킷 송신 과정에서도 transport error가 발생할 수 있으므로,
해당 결과를 다시 HandleTransportException() 예외 처리 함수에 전달합니다.

단, 에러 패킷 송신 결과는
다시 `protocol_error == true`가 될 수 없으므로
무한 재귀 구조는 발생하지 않습니다.

### 추후 PacketType 확장

추후 Chat Server 단계에서는 다음과 같은 확장도 가능합니다.

```text
JOIN
  → 입장 알림

LEAVE
  → 퇴장 알림

SERVER_NOTICE
  → 서버 공지
```

### PacketType과 LogType의 구분

`PacketType`은 네트워크로 송수신되는 패킷의 의미를 나타냅니다.
즉, 실제 `PacketHeader::type` 필드와 연결되는 애플리케이션 레벨 프로토콜 값입니다.

반면 `LineLogger::LogType`은 콘솔 로그의 종류를 나타내기 위한 로깅 전용 타입입니다.

```text
PacketType
  → 네트워크 패킷의 의미
  → PacketHeader::type과 연결됨
  → 송신 / 수신 데이터에 포함됨

LineLogger::LogType
  → 서버 내부 로그 출력의 의미
  → 콘솔 출력 형식에 사용됨
  → 네트워크 패킷에는 포함되지 않음
```

예를 들어 `PacketType::HEADER_ERROR`는 실제 에러 패킷을 나타내는 프로토콜 값이고,
`LineLogger::LogType::PROTOCOL_ERROR`는 서버가 해당 상황을 로그로 기록하기 위한 출력 타입입니다.

따라서 두 enum class는 목적이 다릅니다.
`PacketType`은 프로토콜 문맥에서 관리하고,
`LogType`은 로깅 문맥에서 관리합니다.

### length

`length`는 payload 길이를 나타냅니다.

현재 최대 payload 길이는 다음과 같습니다.

```text
4096 bytes
```

현재 정책은 다음과 같습니다.

```text
length == 0
  → protocol error

length > PAYLOAD_SIZE
  → protocol error
```

`length`가 허용 범위를 벗어나면 payload를 수신하지 않고 protocol error로 판단합니다.

## 3. Payload

Payload는 실제 메시지 데이터입니다.

현재 정책은 다음과 같습니다.

- 최대 4096 bytes
- 문자열의 null 문자 `\0`는 송신하지 않음
- 수신 측에서 출력용 null 문자를 직접 추가
- `length`를 기준으로 정확히 payload 크기를 판단

즉, 문자열 종료 여부를 `\0`에 의존하지 않고,
헤더의 `length`를 기준으로 메시지 크기를 판단합니다.

---

## 4. send_all()

`send_all()`은 요청한 길이만큼 반복해서 `send()`를 호출하는 helper 함수입니다.

### 역할

- 요청한 바이트 수만큼 반복 송신
- partial send 발생 시 남은 바이트 계속 송신
- `SOCKET_ERROR` 발생 시 즉시 반환

개념적 흐름은 다음과 같습니다.

```text
보내야 할 전체 길이 = len
이미 보낸 길이 = 0

while 이미 보낸 길이 < 전체 길이:
    send()
    성공하면 이미 보낸 길이 증가
    실패하면 transport error
```

`send_all()`이 필요한 이유는 `send()` 한 번이 요청한 모든 바이트를 보낸다는 보장이 없기 때문입니다.

---

## 5. recv_all()

`recv_all()`은 요청한 길이만큼 반복해서 `recv()`를 호출하는 helper 함수입니다.

### 역할

- 요청한 바이트 수만큼 반복 수신
- partial recv 발생 시 남은 바이트 계속 수신
- `SOCKET_ERROR` 발생 시 즉시 반환
- `recv() == 0`이면 peer graceful shutdown으로 판단

개념적 흐름은 다음과 같습니다.

```text
받아야 할 전체 길이 = len
이미 받은 길이 = 0

while 이미 받은 길이 < 전체 길이:
    recv()
    성공하면 이미 받은 길이 증가
    recv() == 0이면 peer exit
    실패하면 transport error
```

`recv_all()`이 필요한 이유는 TCP가 byte stream 기반이기 때문에,
헤더나 payload가 여러 번에 나뉘어 도착할 수 있기 때문입니다.

---

## 6. 송수신 순서

현재 기본 수신 흐름은 `ClientSession::RecvPacket()`으로 분리되어 있습니다.

```text
1. PacketHeader 수신
2. network byte order → host byte order 변환
3. length 검사
4. type 검사
5. payload 길이만큼 payload 수신
6. 문자열 출력용 null 문자 추가
7. 수신 결과를 RecvResult로 반환
```

`RecvPacket()`은
수신 과정의 성공/실패를 나타내는 `NetState`와 정상 수신된 패킷의 타입 / 페이로드를 함께 담은 `RecvResult`를 반환합니다.

`HEADER_ERROR` 타입의 패킷은 수신 실패가 아닌 **정상 수신된 에러 알림 패킷**입니다.
따라서 `RecvPacket()` 내부에서 `NetState`에 기록하지 않고,
`RecvResult`의 `type` 필드를 통해 호출자에게 전달합니다.

현재 기본 송신 흐름은 `ClientSession::SendPacket()`으로 분리되어 있습니다.

```text
1. payload 길이 검사
2. PacketHeader 구성
3. host byte order → network byte order 변환
4. PacketHeader 송신
5. Payload 송신
```

즉, `Run()`은 byte order 변환이나 Header / Payload 송수신 세부 절차를 직접 알 필요가 없습니다.

```text
Run()
  → RecvPacket()        → RecvResult 반환
  → 수신 상태 확인       → 문제 있으면 HandleTransportException(state)
  → HandleRecvPacket()  → 패킷 타입별 처리 (echo, 세션 종료 등)
  → closing 확인        → true이면 루프 종료
```

수신 결과 처리는 두 경로로 분리됩니다.

```text
RecvResult.state에 문제가 있는 경우
  → HandleTransportException(state)
  → transport error / protocol error / peer closed 처리

RecvResult.state가 정상인 경우
  → HandleRecvPacket(res)
  → PacketType별 처리
    CHAT_MESSAGE   → SendPacket() (echo)
    HEADER_ERROR   → MarkClosing() (정상 수신된 에러 알림 패킷)
    NICKNAME_CHANGE → 미구현 stub
```

이 분리를 통해 `HandleTransportException()`은 수신 과정 자체의 실패만 담당하고,
수신된 패킷의 의미에 따른 처리는 `HandleRecvPacket()`이 담당합니다.

동시성 면에서 중요한 점은 Header와 Payload가 논리적으로 하나의 패킷이라는 것입니다.

따라서 추후 멀티스레드 broadcast 구조에서는
같은 클라이언트에게 다음과 같은 송신 순서가 발생하면 안 됩니다.

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

이 문제는 추후 `ClientSession`별 `send_mutex`를 `SendPacket()` 내부에 적용하여 해결할 예정입니다.

## 7. NetState

`NetState`는 통신 과정에서 현재 어떤 단계가 완료되었는지,
또 어떤 예외 상황이 발생했는지를 기록하기 위한 상태 관리 구조체입니다.

### 역할

- header 송신 여부
- payload 송신 여부
- header 수신 여부
- payload 수신 여부
- transport error 발생 여부
- peer exit 여부
- protocol error 발생 여부

멀티스레드 구조에서는 각 클라이언트별 송수신 상태가 독립적이어야 합니다.

따라서 `NetState`는 `ClientSession`의 멤버로 둡니다.

```cpp
class ClientSession {
private:
    NetState ClientState;
};
```

`ClientSocket`이 `NetState&`를 멤버로 들고 있을 필요는 없습니다.

대신 송신 / 수신 시점에 `ClientSession`이 소유한 `ClientState`를
송수신 함수에 레퍼런스로 넘겨 상태 변화를 반영합니다.

```cpp
ClientSock->ClientSockSend(ClientState, msg, len);
ClientSock->ClientSockRecv(ClientState, buf, len);
```

---

## 8. 에러 구분

현재 프로젝트에서는 다음 상황을 구분합니다.

### protocol error

애플리케이션 레벨 프로토콜 규칙을 어긴 경우입니다.

예:

- header의 `length`가 허용 범위를 벗어남
- 정의되지 않은 `type` 수신

`HEADER_ERROR` 타입의 패킷은 여기에 해당하지 않습니다.
상대가 `HEADER_ERROR` 타입을 올바른 형식으로 보낸 경우는 **정상 수신된 에러 알림 패킷**이므로,
`NetState`의 protocol error로 기록하지 않고 `RecvResult.type`을 통해 반환합니다.
이후 `HandleRecvPacket()`이 해당 타입을 보고 세션 종료 처리를 수행합니다.

### transport error

소켓 송수신 자체에서 에러가 발생한 경우입니다.

예:

- `send()`가 `SOCKET_ERROR` 반환
- `recv()`가 `SOCKET_ERROR` 반환

### peer exit

상대방이 정상적으로 연결을 종료한 경우입니다.

예:

- `recv()`가 `0` 반환

---

## 9. SessionID와 프로토콜의 관계

`SessionID`는 서버 내부에서 `ClientSession`을 식별하기 위한 값입니다.

현재 구조에서 `SessionID`는 다음 용도로 사용합니다.

```text
ClientManager 내부 세션 관리
RemoveClient(SessionID id)
로그 출력
추후 세션 조회
```

반면, `SessionID`는 현재 애플리케이션 레벨 패킷의 필드는 아닙니다.

```text
PacketHeader
  ├── type
  └── length

SessionID
  → 서버 내부 관리용 ID
```

따라서 `PacketHeader` 구조는 그대로 유지합니다.

```cpp
#pragma pack(push, 1)
struct PacketHeader {
    int32_t type;
    uint32_t length;
};
#pragma pack(pop)
```

추후 클라이언트에게 자신의 세션 ID를 알려주거나,
서버 메시지에 세션 ID를 포함하고 싶다면 별도의 message type 또는 payload 형식으로 확장할 수 있습니다.
현재 단계에서는 `SessionID`를 네트워크 프로토콜에 포함하지 않고 서버 내부 관리용으로만 사용합니다.

---

## 10. Chat Server 확장 방향

현재 `PacketHeader`의 `type` 필드는 추후 Chat Server 단계에서 확장할 수 있습니다.

현재 확장 결과:

```text
CHAT_MESSAGE
  → 일반 채팅 메시지

NICKNAME_CHANGE
  → 닉네임 변경 메시지
```

예상 확장 방향:

```text
MESSAGE_TYPE_JOIN
  → 입장 알림

MESSAGE_TYPE_LEAVE
  → 퇴장 알림
```

현재 단계에서는 Echo Server 로직을 안정화하는 것이 우선이며,
message type 확장은 Broadcast Chat 단계에서 본격적으로 진행할 예정입니다.

---

## 11. 정리

현재 프로토콜의 핵심은 다음과 같습니다.

```text
TCP는 message boundary를 보장하지 않는다.
따라서 PacketHeader + Payload 구조로 메시지 경계를 직접 정의한다.
```

그리고 실제 송수신에서는 다음 원칙을 지킵니다.

```text
send() / recv() 한 번을 믿지 않고,
send_all() / recv_all()로 원하는 길이만큼 반복 처리한다.
```

26.05.20(수) 리팩토링 이후에는 이 패킷 송수신 절차가 `ClientSession::RecvPacket()`과
`ClientSession::SendPacket()`으로 분리되었습니다.

```text
RecvPacket()
  → PacketHeader + Payload 단위의 애플리케이션 패킷을 수신한다.

SendPacket()
  → PacketHeader + Payload 단위의 애플리케이션 패킷을 송신한다.
```

따라서 Echo Server 단계에서는 `Run()`이 `RecvPacket()` 후 `SendPacket()`을 호출하고,
Chat Server 단계에서는 `RecvPacket()` 후 `ClientManager::Broadcast()`로 확장할 수 있습니다.
