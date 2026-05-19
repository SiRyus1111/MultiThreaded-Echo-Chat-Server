# Roadmap

이 문서는 `MultiThreaded Echo Server`의 현재 구현 상태,
구현 예정 기능, 향후 개선 방향을 정리합니다.

---

## 1. 현재 단계

현재 프로젝트는 다음 단계에 있습니다.

```text
기존 단일 클라이언트 Echo Server
        ↓
멀티클라이언트 / 멀티스레드 Echo Server  ← 현재 단계
        ↓
브로드캐스트 Chat Server
```

현재 목표는 완성된 Chat Server가 아니라,
**멀티클라이언트 Echo Server를 안정적으로 구성하기 위한 서버 기틀을 만드는 것**입니다.

---

## 2. 구현 완료

### 기존 Echo Server 기반

- TCP 기반 Echo Server / Client
- TCP byte stream 특성을 고려한 partial send / recv 처리
- `send_all()` / `recv_all()` helper 함수
- `PacketHeader(type, length)` 기반 메시지 구조
- protocol error / transport error / peer exit 구분 처리
- `NetState` 기반 통신 상태 관리
- OOP / RAII 기반 socket 자원 관리 구조

### Socket RAII 구조

- `WinsockGuard` 설계 및 구현
- `ListenSocket` 설계 및 구현
- `ClientSocket` 설계 및 구현
- `ConnectSocket` 설계 및 구현
- socket handle 복사 금지
- socket 소유권 이동 구조 적용

### 멀티클라이언트 기본 구조

- 기존 단일 클라이언트 Echo Server 로직을 `ClientSession` 기반 구조로 이식
- 클라이언트마다 독립적인 `ClientSession` 객체 생성
- `ClientSession` 내부에 `ClientSocket`, `ClientAddr`, `Manager_wp`, `ClientState`, `closing` 배치
- `ClientSession` 생성자에서 `unique_ptr<ClientSocket>` 소유권 이동 처리
- `std::make_shared<ClientSession>()`를 통한 세션 객체 생성 구조 적용

### Thread 구조

- 클라이언트별 `std::thread` 생성
- `client_thread(std::shared_ptr<ClientSession>)` 구조 적용
- `std::thread` 생성 직후 `detach()`하는 구조 적용
- detached thread가 `shared_ptr<ClientSession>`을 들고 `Run()` 실행

### 세션 관리

- `ClientManager` 기본 구조 구현
- `ClientSession` 기본 구조 구현
- `ClientManager`가 현재 접속 중인 `ClientSession` 목록 관리
- `ClientSession::RemoveThisClient()` 구조 추가
- `ClientManager::RemoveClient(std::shared_ptr<ClientSession>)` 임시 구현
- `std::atomic<bool> closing`을 통한 논리적 종료 상태 관리

### ClientSession 송수신 함수 분리

- `ClientSession::RecvPacket()` 1차 구현
- `ClientSession::SendPacket()` 1차 구현
- `Run()`에서 Header / Payload 송수신 세부 로직 분리
- `Run()`은 Echo Server의 고수준 흐름 제어만 담당하도록 정리
- `PacketType` enum class 도입
- `PacketHeader::type`은 `int32_t` 전송 필드로 유지하고, `PacketType`은 코드 내부 의미 표현용으로 사용
- payload length가 `0`이거나 `PAYLOAD_SIZE`를 초과하면 protocol error로 처리
- `TransportExceptionHandling()`을 통해 통신 오류 / peer exit / protocol error 후처리 흐름 정리
- `ClientAddrStr`을 통해 클라이언트 IP 문자열을 세션 생성 시점에 저장

### 객체 소유권

- `shared_ptr<ClientSession>` 기반 세션 생존 관리
- `unique_ptr<ClientSocket>` 기반 socket 독점 소유
- `weak_ptr<ClientManager>` 기반 Manager 비소유 참조
- `enable_shared_from_this` 기반 `shared_from_this()` 사용 구조

---

## 3. 현재 기준 미구현

현재 기준으로 다음 기능은 아직 구현되지 않았습니다.

- Broadcast Chat 기능
- `SessionID`
- 로그 출력 형식 개선
- nickname 기반 클라이언트 식별
- 메시지 타입별 채팅 프로토콜 분기
- ClientSession별 `send_mutex`
- broadcast 중 송신 실패 세션 정리 정책
- clients snapshot 기반 broadcast 구조

---

## 4. 단기 구현 목표

### 4-1. SessionID 도입

`SessionID`의 목적은 다음과 같습니다.

```text
클라이언트 세션들을 구분해서
ClientManager가 특정 세션을 쉽게 찾을 수 있도록 한다.
```

IP:Port만으로는 세션 식별에 한계가 있으므로,
서버 내부에서 고유한 `SessionID`를 부여합니다.

초기 구현 방향:

```text
using SessionID = uint64_t;
```

예상 구조:

```text
ClientManager
  └── unordered_map<SessionID, shared_ptr<ClientSession>>
```

단기적으로는 기존 `vector<shared_ptr<ClientSession>>`를 유지하되,
`SessionID` 필드부터 추가할 수 있습니다.

---

### 4-2. 로그 출력 개선

현재 `ClientSession`은 생성 시점에 `ClientAddrStr`을 저장합니다.
이를 기반으로 다음 단계에서는 로그 형식을 정리합니다.

예상 형식:

```text
[Session ?][127.0.0.1:53021][RecvPacket] header received
[Session ?][127.0.0.1:53021][RecvPacket] payload received
[Session ?][127.0.0.1:53021][SendPacket] packet sent
[Session ?][127.0.0.1:53021][Close] peer exit
```

`SessionID`가 도입되면 로그에 `SessionID`도 함께 출력하여
멀티클라이언트 상황에서 어떤 세션의 로그인지 쉽게 구분할 수 있게 합니다.

---

### 4-3. send_mutex 추가

Chat Server 단계에서는 여러 thread가 같은 `ClientSession`에게
동시에 `SendPacket()`을 호출할 가능성이 있습니다.

따라서 같은 클라이언트에 대한 Header / Payload 송신 순서를 보호하기 위해
`ClientSession`별 `send_mutex`를 추가합니다.

```cpp
class ClientSession {
private:
    std::mutex send_mutex;

public:
    NetState SendPacket(const char* msg, uint32_t len, PacketType type);
};
```

목표:

```text
Header_A
Payload_A
Header_B
Payload_B
```

잘못된 순서:

```text
Header_A
Header_B
Payload_A
Payload_B
```

---

### 4-4. ClientManager::Broadcast() 구현

초기 Broadcast 구조는 다음을 목표로 합니다.

```text
1. clients_mutex를 잡는다.
2. clients snapshot을 복사한다.
3. clients_mutex를 해제한다.
4. snapshot을 순회한다.
5. closing == true인 세션은 건너뛴다.
6. 각 세션의 SendPacket()을 호출한다.
```

목표:

- manager lock을 오래 잡지 않기
- `send()`를 manager lock 내부에서 수행하지 않기
- 같은 세션에 대한 패킷 경계 보호
- 종료 예정 세션 송신 제외

---

## 5. 중기 구현 목표

### 5-1. Broadcast Chat Server

Echo Server 구조를 기반으로,
한 클라이언트가 보낸 메시지를 다른 클라이언트들에게 전달합니다.

```text
Client A → Server → Client B
                  → Client C
                  → Client D
```

결정할 정책:

- sender 자신에게도 메시지를 보낼 것인가?
- 입장 / 퇴장 메시지를 broadcast할 것인가?
- 송신 실패한 클라이언트를 언제 제거할 것인가?
- `closing == true` 세션을 snapshot 단계에서 제외할 것인가, 순회 단계에서 제외할 것인가?

---

### 5-2. nickname 도입

채팅 서버로 확장하면 클라이언트 표시 이름이 필요합니다.

예상 추가 필드:

```cpp
class ClientSession {
private:
    std::string nickname;
};
```

고려할 점:

- 기본 nickname 부여 방식
- 중복 nickname 허용 여부
- nickname 변경 메시지 타입
- 접속 / 퇴장 메시지 출력 형식

---

### 5-3. message type 확장

현재 `PacketHeader.type`은 일반 메시지 / 에러 메시지 구분에 사용됩니다.

Chat Server 단계에서는 message type을 확장합니다.

예상 타입:

```text
CHAT_MESSAGE
JOIN
LEAVE
NICKNAME_CHANGE
SERVER_NOTICE
ERROR_MESSAGE
```

---

## 6. 장기 개선 목표

### 6-1. detach 기반 구조 검증

현재는 `detach()` 기반 구조를 사용합니다.

장기적으로는 다음을 검토합니다.

- detached thread 종료 관측 방식
- `promise` / `future`를 통한 종료 사유 전달
- thread pool 구조
- join 가능한 thread 관리 구조
- 서버 종료 시 모든 client thread 정리 방식

---

### 6-2. send queue 구조 검토

현재 계획은 `Broadcast()`가 각 세션의 `SendPacket()`을 직접 호출하는 구조입니다.

추후 느린 클라이언트 문제가 커지면 다음 구조를 검토합니다.

```text
ClientSession
  ├── send_queue
  ├── send_mutex
  ├── condition_variable
  └── sender loop
```

---

### 6-3. select() 기반 멀티플렉싱 서버 검토

thread-per-client 구조를 구현한 뒤,
다음 단계로 select() 기반 멀티플렉싱 서버를 검토할 수 있습니다.

목표:

- blocking thread-per-client 구조와 비교
- 여러 socket을 하나의 thread에서 감시
- read readiness / write readiness 이해
- 이후 IOCP 같은 Windows 고성능 I/O 모델로 확장할 기반 마련

---

## 7. 테스트 체크리스트

### Echo Server 단계

- [v] 클라이언트 1개 접속 후 echo 정상 동작
- [v] 클라이언트 여러 개 접속 후 각자 echo 정상 동작
- [v] 한 클라이언트 종료 시 다른 클라이언트 영향 없음
- [v] payload 길이 0 또는 비정상 length 처리
- [v] 최대 payload 길이 근처 메시지 처리
- [v] partial send / recv 상황에서도 정상 동작
- [ ] `RecvPacket()` / `SendPacket()` 분리 후 Echo 동작 재확인
- [ ] `PacketType` 변환 및 type 검증 테스트
- [v] transport error 발생 시 세션 종료 처리
- [v] peer exit 발생 시 세션 제거 처리

### ClientManager 단계

- [v] AddClient 정상 동작
- [v] RemoveClient 정상 동작
- [ ] 동시에 여러 클라이언트 종료 시 clients 컨테이너 안전성 확인
- [ ] RemoveClient 이후 마지막 shared_ptr 소멸 시점 확인
- [ ] closing 상태가 broadcast 대상 제외에 사용되는지 확인

### Broadcast 단계

- [ ] 한 클라이언트 메시지가 다른 클라이언트에게 전달됨
- [ ] sender 자신에게 보낼지 정책 확인
- [ ] `closing == true` 세션은 송신 제외
- [ ] broadcast 중 클라이언트 종료 상황 처리
- [ ] Header / Payload 순서가 섞이지 않음
- [ ] 느린 클라이언트가 있을 때 서버 전체가 과도하게 막히지 않는지 확인

---

## 8. 문서 관리 계획

현재 README에 몰려 있던 내용을 다음 문서로 분리했습니다.

```text
README.md
  → 프로젝트 대표 소개

docs/architecture.md
  → 전체 구조와 책임 분리

docs/protocol.md
  → 패킷 구조와 TCP byte stream 처리

docs/socket-raii.md
  → Winsock2 socket RAII 관리

docs/concurrency-design.md
  → 소유권, thread, 종료 상태, broadcast lock 설계

docs/roadmap.md
  → 구현 상태와 향후 계획

docs/original-design-note.md
  → 기존 README 원본 보존
```

향후 구현이 진행되면 README는 짧게 유지하고,
상세한 설계 변경은 `docs` 아래 문서에 반영합니다.

---

## 9. 현재 우선순위

현재 기준 우선순위는 다음과 같습니다.

```text
1. Echo Server 멀티클라이언트 구조 안정화
2. SessionID 도입
3. 로그 출력 형식 개선
4. send_mutex 추가
5. Broadcast 구현
6. nickname / message type 확장
7. Chat Server로 확장
```

즉, 지금 당장 중요한 것은 화려한 채팅 기능이 아니라,
세션 수명과 동기화 구조가 깨지지 않는 안정적인 서버 기틀을 만드는 것입니다.
