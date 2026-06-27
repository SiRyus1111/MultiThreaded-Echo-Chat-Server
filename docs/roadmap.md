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
- `std::atomic<bool> closing`을 통한 논리적 종료 상태 관리
- `SessionID` 1차 도입
- `ClientSession`에 `SessionID session_id` 멤버 추가
- `ClientManager`의 clients 컨테이너를 `unordered_map<SessionID, shared_ptr<ClientSession>>`로 변경
- `ClientManager::AddClient(std::shared_ptr<ClientSession>, SessionID)` 구조 적용
- `ClientManager::RemoveClient(SessionID)` 구조 적용
- `ClientSession::RemoveThisClient()`가 `shared_from_this()` 대신 `session_id` 기반으로 제거 요청하도록 변경

### ClientSession 송수신 함수 분리

- `ClientSession::RecvPacket()` 1차 구현
- `ClientSession::SendPacket()` 1차 구현
- `Run()`에서 Header / Payload 송수신 세부 로직 분리
- `Run()`은 Echo Server의 고수준 흐름 제어만 담당하도록 정리
- `PacketType` enum class 도입
- `PacketHeader::type`은 `int32_t` 전송 필드로 유지하고, `PacketType`은 코드 내부 의미 표현용으로 사용
- payload length가 `0`이거나 `PAYLOAD_SIZE`를 초과하면 protocol error로 처리
- `HandleTransportException()`을 통해 통신 오류 / peer exit / protocol error 후처리 흐름 정리
- `ClientAddrStr`을 통해 클라이언트 IP 문자열을 세션 생성 시점에 저장
- `SendPacket()` / `RecvPacket()` 각 함수의 지역 `NetState` 객체 (`send_packet_state` / `recv_packet_state`) 도입
- `ClientState`는 세션 전체 상태, 지역 `NetState`는 해당 함수 호출의 결과 상태로 역할 분리
- `SendPacket()` / `RecvPacket()`의 반환값을 `send_packet_state` / `recv_packet_state`로 재설정

### 객체 소유권

- `shared_ptr<ClientSession>` 기반 세션 생존 관리
- `unique_ptr<ClientSocket>` 기반 socket 독점 소유
- `weak_ptr<ClientManager>` 기반 Manager 비소유 참조
- `enable_shared_from_this` 기반 `shared_from_this()` 사용 구조

### Logging

- `LineLogger` 공용 로깅 라이브러리 구현
- 싱글톤 패턴 기반 전역 단일 로거 구조 적용
- 전역 단일 `output_mutex_`로 `std::cout` 보호 정책 통일
- `LogType` enum class 기반 로그 타입 제한 구조 구현
- `LogTypeToCstyleString()` 기반 로그 타입 문자열 변환 중앙화
- 가변 인자 기반 로그 생성 구조 구현
- 기존 `std::cout`과 비슷한 사용성을 위한 `WriteLog(Args&&... args)` 구현
- 로그 한 줄 단위 출력 구조 구현
- `std::ostringstream` 기반 로그 문자열 선조립 구조 구현
- 완성된 로그 문자열을 한 번의 `std::cout << oss.str()` 연산으로 출력하는 구조 구현
- 출력 구간 최소 범위 mutex 보호 적용
- `SessionID` / `IP:Port` / `LogType` 기반 표준 세션 로그 형식 설계

### PacketType 리팩토링

- `PacketType::SAFE`를 `PacketType::CHAT_MESSAGE`로 변경
- `PacketType::NICKNAME_CHANGE` 추가
- `PacketType` 값 재정의 (`CHAT_MESSAGE = 1`, `NICKNAME_CHANGE = 2`, `HEADER_ERROR = 3`)
- 서버 / 클라이언트 코드 전체에 적용 완료

### 클라이언트 리팩토링

- `ParsedInput` 구조체 구현
- `InputParser` 클래스 및 `static Parse()` 함수 구현
- `/nick`, `/quit`, 일반 메시지, 유효하지 않은 명령어 파싱 처리
- `ClientApp` 클래스 구현 (`ConnectSocket` 캡슐화, `state_`, `nick_` 소유)
- `ClientApp::SendPacket()` 구현 (`ClientSession::SendPacket()`과 동일한 상태 기록 정책 적용)
- `ClientApp::RecvPacket()` 구현 (`ClientSession::RecvPacket()`과 동일한 상태 기록 정책 적용)
- `ClientApp::Run()` 구현 (입력 → 파싱 → 송신 → 수신 → 출력 루프)
- `main()`에서 `ClientApp` 기반 구조로 전환
- 클라이언트에 `LineLogger::WriteLog()` 1차 적용
- `LineLogger::WriteInputLog()` 추가 — 줄바꿈 없는 프롬프트 출력 전용 함수
- `ClientSession`에 `Nickname` 멤버 추가 및 기본값(`user_{session_id}`) 설정


### RecvResult 도입 및 패킷 핸들러 구조 추가

- `RecvResult` 구조체 구현 (서버 / 클라이언트 공통) — `NetState`, `PacketType`, `length`, `payload` 포함
- `RecvPacket()` 반환값을 `RecvResult`로 변경 — 수신 과정의 성공/실패(`NetState`)와 수신된 패킷의 타입(`PacketType`)을 분리하여 반환
- `HandleRecvPacket()` 구현 (서버 / 클라이언트 공통) — 정상 수신된 패킷을 타입별로 처리하는 패킷 핸들러
- `HandleTransportException()` 책임 재정의 — 수신 과정 자체의 실패만 처리, `HEADER_ERROR` 수신은 `HandleRecvPacket()`으로 이관
- `TransportExceptionHandling()` → `HandleTransportException()` 이름 변경
- `ClientApp`에 `closing` / `MarkClosing()` 추가

### 닉네임 시스템

- `PacketType` 추가 — `NICKNAME_CHANGE_FAILED = 4`, `NICKNAME_CHANGE_SUCESS = 5`
- `PacketHeader`에 `char nickname[32]` 필드 추가 (`#pragma pack(push, 1)` 적용)
- `RecvResult`에 `Nickname nick` 필드 추가 (수신 패킷의 송신자 닉네임)
- `ClientSession`에 `Nickname nickname` 멤버 추가
- `ClientSession` 생성자에서 `user_(session_id)` 형태의 기본 닉네임 자동 설정 (`ostringstream` 사용)
- `ClientSession` getter 함수 6종 추가 — `GetState()`, `GetClosing()`, `GetSessionID()`, `GetNickname()`, `GetBinaryAddr()`, `GetStrAddr()`
  (현재 미사용, 추후 직접 접근 패턴 정리 시 적용 예정)
- `ClientManager::GetClients()` 추가 — `clients_mutex` 보호 하에 clients snapshot 반환; `Manager_wp.lock()` 패턴으로 접근
- `ClientSession::SendPacket()` 시그니처에 `Nickname nick` 파라미터 추가; nickname 패딩 (`memset` → `memcpy`) 처리
- `ClientSession::RecvPacket()`에서 33바이트 버퍼로 nickname 파싱 (`[32]`에 `'\0'` 강제 추가); `result.nick` 설정
- `ClientSession::HandleRecvPacket()`에 `NICKNAME_CHANGE` 처리 구현
  — 32바이트 길이 검사, `GetClients()` snapshot 기반 중복 검사 (`res.payload` 비교), 닉네임 갱신, 성공/실패 패킷 송신
- `ClientSession::HandleTransportException()` 내 `WriteSessionLog()` 호출 전체에 `nickname` 파라미터 추가
- `ECHO_NICK = "EchoFromServer"` 상수 추가 — echo 패킷 헤더 닉네임용
- `SERVER_NICK = "ServerMessage"` 상수 추가 — 서버 알림 패킷 헤더 닉네임용
- `LineLogger::WriteSessionLog()` 시그니처에 `std::string nickname` 파라미터 추가; 출력 형식에 `[Nickname name]` 추가
- `LineLogger::WriteChatLog()` 신규 추가 — 클라이언트 수신 메시지 전용 출력 (`[nickname] message` 형식)
- `InputParser::Parse()`에 닉네임 32바이트 초과 검사 추가 (`valid = false`)
- `ClientApp` 생성자에 빈 닉네임 초기화 (`nick_.resize(MAX_NICKNAME_LENGTH, '\0')`)
- `ClientApp::Run()`에 초기 닉네임 설정 루프 추가 — 메인 루프 진입 전 서버와 닉네임 동기화
- `ClientApp::SendPacket()` 시그니처에 `Nickname nick` 파라미터 추가
- `ClientApp::HandleRecvPacket()`에 `NICKNAME_CHANGE_SUCESS` / `NICKNAME_CHANGE_FAILED` 처리 추가
  — 성공 시 `nick_` = `res.payload`로 갱신

---

## 3. 현재 기준 미구현

현재 기준으로 다음 기능은 아직 구현되지 않았습니다.

### Logging (미구현)

- `ClientManager` 로그의 `LineLogger` 적용
- 서버 전역 로그의 `LineLogger` 적용
- low-level transport 계층 로그 정책 검토
- `WriteTransportLog()` 확장 여부 검토
- transport error / protocol error / peer error 로그 점검
- `LogType` 목록이 실제 로그 정책을 충분히 표현하는지 검증

### 클라이언트 (미구현)

- `HandleTransportException()` 내부 `std::cout` 출력을 `LineLogger` 기반으로 교체

### Others

- Broadcast Chat 기능
- nickname 기반 클라이언트 식별
- 메시지 타입별 채팅 프로토콜 분기
- ClientSession별 `send_mutex`
- broadcast 중 송신 실패 세션 정리 정책
- clients snapshot 기반 broadcast 구조

---

## 4. 단기 구현 목표

### 4-1. SessionID 기반 구조 검증

`SessionID`는 1차 도입되었지만,
아직 세부 정책과 주변 구조는 더 검증할 필요가 있습니다.

현재 설계의 목적은 다음과 같습니다.

```text
클라이언트 세션들을 구분해서
ClientManager가 특정 세션을 쉽게 찾고 제거할 수 있도록 한다.
```

현재 방향:

```cpp
using SessionID = uint64_t;
const SessionID INITIAL_SESSION_ID = 0;
```

현재 구조:

```text
ClientManager
  └── unordered_map<SessionID, shared_ptr<ClientSession>>

ClientSession
  └── SessionID session_id
```

현재 제거 흐름:

```text
ClientSession::RemoveThisClient()
  → ClientManager::RemoveClient(session_id)
  → clients.erase(session_id)
```

남은 검증 사항:

- `SessionID` 부여 시점이 accept 직후로 적절한지 확인
- `next_session_id` 증가가 main thread에서만 일어나는 현재 구조 유지
- 추후 multi-accept 구조로 바뀔 경우 atomic 또는 mutex 필요성 검토
- 로그 출력에 `SessionID`를 포함하는 형식 정리
- `ClientSession`의 `enable_shared_from_this` 상속이 계속 필요한지 검토
- `clients[id] = client` 대신 `emplace()`를 사용할지 검토
- `unordered_map` 기반 broadcast snapshot 생성 방식 정리

### 4-2. LineLogger 프로젝트 적용

현재 `LineLogger` 라이브러리는 구현되었지만,
프로젝트 전체의 기존 콘솔 출력이 아직 모두 `LineLogger` 기반으로 교체된 것은 아닙니다.

현재 구현된 `LineLogger`의 핵심 구조는 다음과 같습니다.

```text
LineLogger::GetInstance()
  → 전역 단일 LineLogger 객체 반환
  → 모든 콘솔 출력이 동일한 output_mutex_ 공유
```

또한 로그 타입은 문자열이 아니라 `LogType` enum class로 제한합니다.

```cpp
LineLogger::LogType::CONNECTED
LineLogger::LogType::RECEIVING
LineLogger::LogType::RECV_COMPLETE
LineLogger::LogType::SENDING
LineLogger::LogType::SEND_COMPLETE
LineLogger::LogType::DISCONNECTED
LineLogger::LogType::PROTOCOL_ERROR
LineLogger::LogType::TRANSPORT_ERROR
```

단기 목표는 다음입니다.

- 기존 `std::cout` 출력 지점 검색
- 서버 전역 로그를 `LineLogger::GetInstance().WriteLog()`로 교체
- 세션 관련 로그를 `WriteSessionLog()`로 교체
- 연결 성공 시 `CONNECTED` 로그 추가
- 수신 시작 / 수신 완료 로그 위치 정리
- 송신 시작 / 송신 완료 로그 위치 정리
- 정상 종료 시 `DISCONNECTED` 로그 추가
- protocol error / transport error / peer error 로그 정리
- 모든 콘솔 출력이 하나의 `LineLogger` 객체와 하나의 mutex를 공유하도록 통일

예상 세션 로그 형식은 다음과 같습니다.

```text
[SessionID 3][127.0.0.1:53021][CONNECTED] client connected
[SessionID 3][127.0.0.1:53021][RECV_COMPLETE] payload received
[SessionID 3][127.0.0.1:53021][SEND_COMPLETE] payload sent
[SessionID 3][127.0.0.1:53021][DISCONNECTED] peer disconnected
```

이 작업이 완료되면 README와 설계 문서에는 “LineLogger 라이브러리 구현 완료”가 아니라
“프로젝트 콘솔 출력의 LineLogger 통합 완료”로 상태를 갱신할 수 있습니다.

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

현재 구현 완료 상태:

- `PacketHeader`에 `char nickname[32]` 고정 필드 추가
- `PacketType::NICKNAME_CHANGE_FAILED` / `NICKNAME_CHANGE_SUCESS` 추가
- 클라이언트에서 `/nick` 명령어 파싱 구현 완료 (`InputParser`, 32바이트 초과 검사 포함)
- `ClientSession::nickname` 멤버 추가; 기본 닉네임 `user_(session_id)` 자동 설정
- `ClientSession::HandleRecvPacket()`에 `NICKNAME_CHANGE` 처리 구현 (길이 검사, 중복 검사, 닉네임 갱신)
- `ClientManager::GetClients()` 추가 (닉네임 중복 검사에 활용)
- `ClientApp::Run()` 초기 닉네임 설정 루프 추가 (서버-클라이언트 닉네임 동기화)
- `ClientApp::HandleRecvPacket()` — `NICKNAME_CHANGE_SUCESS` / `NICKNAME_CHANGE_FAILED` 처리 완료

향후 과제:

- Broadcast 단계에서 닉네임 기반 입장 / 퇴장 메시지 출력
- 닉네임 기반 클라이언트 식별 로그 개선

---

### 5-3. message type 확장

현재 진행 상태:

- `CHAT_MESSAGE`, `NICKNAME_CHANGE` 확정 및 구현 완료
- `JOIN`, `LEAVE`, `SERVER_NOTICE` 등은 추후 확장 예정

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
- [v] `RecvPacket()` / `SendPacket()` 분리 후 Echo 동작 재확인
- [v] `PacketType` 변환 및 type 검증 테스트
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
- [v] sender 자신에게 보낼지 정책 확인
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
1. LineLogger 프로젝트 전면 적용 (서버 전역 로그, ClientManager 로그 교체)
2. send_mutex 추가
3. ClientManager::Broadcast() 구현
4. Chat Server로 확장
```

즉, 지금 당장 중요한 것은 화려한 채팅 기능이 아니라,
세션 수명과 동기화 구조가 깨지지 않는 안정적인 서버 기틀을 만드는 것입니다.
