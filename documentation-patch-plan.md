# Documentation Patch Plan — 브로드캐스트 도입

이 문서는 브로드캐스트 기능 도입에 따라 기존 문서(`README.md`, `docs/*.md`)에 반영해야 할
수정 / 추가 사항을 문서별로 정리한 패치 예시입니다. 실제 문서에는 아직 반영되지 않았으며,
각 블록을 그대로 복사해 해당 위치에 붙여넣는 용도로 작성했습니다.

설계 의도 / 구현 의도 요약은 대화 본문 참고. 핵심만 다시 적으면:

- `ClientManager::broadcast()`는 `GetClients()` 스냅샷을 순회하며 `closing` 세션과 발신자 자신을
  제외하고 각 세션의 `SendQueuePush()`만 호출한다 (직접 `send()`를 수행하지 않는다).
- `GetClients()`는 순회 전용 스냅샷이므로 `unordered_map` 대신 `vector<shared_ptr<ClientSession>>`을
  반환하도록 개편했다 (`SessionID`는 각 `ClientSession`이 이미 보유하고 있어 map key가 불필요).
- `CHAT_MESSAGE` 브로드캐스트는 헤더의 발신자 닉네임을 덮어쓰지 않고 그대로 전달하므로,
  `ECHO_NICK`은 현재 미사용이지만 향후 재사용 가능성을 위해 상수는 유지한다.
- 발신자 제외 정책은 조건문 하나(`GetSessionID() == sender_id`)로 뒤집을 수 있는 형태를 유지한다.

---

## 1. README.md

### 1-1. §2 구현 예정 — 항목 삭제 (111-112줄)

**삭제할 내용:**

```markdown
- `ClientManager::Broadcast()`
- broadcast 시 clients snapshot 복사 구조
```

### 1-2. §2 구현 완료 — 항목 추가 (103줄 이후)

**추가할 내용:**

```markdown
- `ClientManager::broadcast(std::shared_ptr<Packet> p, SessionID sender_id)` 구현 — clients snapshot(`GetClients()`)을 순회하며 `closing == true`인 세션과 발신자 자신(`sender_id`)을 제외하고 각 세션의 `SendQueuePush()`로 위임
- `ClientManager::GetClients()`를 `unordered_map<SessionID, shared_ptr<ClientSession>>` 반환에서 `vector<shared_ptr<ClientSession>>` 반환으로 개편 — `reserve()`로 재할당 없이 snapshot 복사
- `ClientSession::HandleRecvPacket()`의 `CHAT_MESSAGE` 처리를 자기 자신에게 echo하는 방식에서 `ClientManager::broadcast()` 위임으로 전환 — 헤더 닉네임은 더 이상 `ECHO_NICK`으로 덮어쓰지 않고 발신자의 닉네임을 그대로 전달
```

### 1-3. §7 동기화 설계 요약 — 수정 (275줄, 292-294줄)

**수정 전 (275줄):**

```markdown
추후 Broadcast Chat Server로 확장할 때는 다음 구조를 사용할 예정입니다.
```

**수정 후:**

```markdown
Broadcast Chat Server로 확장하며 다음 구조를 사용합니다.
```

**수정 전 (292-294줄):**

```markdown
따라서 broadcast 시에는 clients 목록을 snapshot으로 복사한 뒤,
`clients_mutex`를 해제하고 각 `ClientSession::SendQueuePush()`로 패킷을 넘기는 구조를 목표로 합니다.
실제 송신(`SendPacket()`)은 broadcast를 호출한 스레드가 아니라, 해당 `ClientSession`을 담당하는 `SendRun()`이 수행합니다.
```

**수정 후:**

```markdown
따라서 `ClientManager::broadcast()`는 clients 목록을 snapshot(`GetClients()`, `vector<shared_ptr<ClientSession>>` 반환)으로 복사한 뒤,
`clients_mutex`를 해제하고 `closing == true`인 세션과 발신자 자신을 제외한 각 `ClientSession::SendQueuePush()`로 패킷을 넘기는 구조로 구현되어 있습니다.
실제 송신(`SendPacket()`)은 broadcast를 호출한 스레드가 아니라, 해당 `ClientSession`을 담당하는 `SendRun()`이 수행합니다.
```

### 1-4. §11 향후 계획 요약(단기 목표) — 항목 삭제 (452줄)

**삭제할 내용:**

```markdown
- `ClientManager::Broadcast()` 구현
```

---

## 2. docs/server-architecture.md

### 2-1. §4 RecvRun() 흐름 — 수정 (174줄)

**수정 전:**

```markdown
  │     ├── CHAT_MESSAGE        → SendQueuePush() (echo, 헤더에 ECHO_NICK 포함)
```

**수정 후:**

```markdown
  │     ├── CHAT_MESSAGE        → ClientManager::broadcast() 위임 (closing 세션 / 발신자 자신 제외 후 각 대상 세션 Send Queue에 push)
```

### 2-2. §6 ClientManager 역할 — 수정 (342줄)

**수정 전:**

```markdown
- 추후 broadcast 수행
```

**수정 후:**

```markdown
- broadcast 수행 (`broadcast()`)
```

### 2-3. §6 ClientManager 코드 블록 — 수정 (346-357줄)

**수정 전:**

```cpp
class ClientManager : public std::enable_shared_from_this<ClientManager> {
private:
    std::unordered_map<SessionID, std::shared_ptr<ClientSession>> clients;
    std::mutex clients_mutex;

public:
    void AddClient(std::shared_ptr<ClientSession> client, SessionID id);
    void RemoveClient(SessionID id);
    std::unordered_map<SessionID, std::shared_ptr<ClientSession>> GetClients();
};
```

**수정 후:**

```cpp
class ClientManager : public std::enable_shared_from_this<ClientManager> {
private:
    std::unordered_map<SessionID, std::shared_ptr<ClientSession>> clients;
    std::mutex clients_mutex;

public:
    void AddClient(std::shared_ptr<ClientSession> client, SessionID id);
    void RemoveClient(SessionID id);
    void broadcast(std::shared_ptr<Packet> p, SessionID sender_id);
    std::vector<std::shared_ptr<ClientSession>> GetClients();
};
```

### 2-4. §8 Echo Server에서 Chat Server로의 확장 — 수정 (396-404줄)

**수정 전:**

```markdown
현재 Echo Server 단계에서는 각 클라이언트가 보낸 메시지를
해당 클라이언트에게 다시 돌려보냅니다.

```text
Client A → Server → Client A
```

추후 Chat Server 단계에서는 `ClientManager::Broadcast()`를 추가하여,
한 클라이언트가 보낸 메시지를 다른 클라이언트들에게 전달합니다.
```

**수정 후:**

```markdown
과거 Echo Server 단계에서는 각 클라이언트가 보낸 메시지를
해당 클라이언트에게 다시 돌려보내는 1:1 echo 구조였습니다.

```text
Client A → Server → Client A
```

현재는 `ClientManager::broadcast()`를 통해,
한 클라이언트가 보낸 메시지를 다른 클라이언트들에게 전달합니다.
```

### 2-5. §8 설계 문제 목록 — 수정 (416줄)

**수정 전:**

```markdown
- sender 자신에게도 메시지를 보낼지 여부
```

**수정 후:**

```markdown
- sender 자신에게도 메시지를 보낼지 여부 → 결정됨: 자신은 제외 (`ClientManager::broadcast()`의 `sender_id` 비교 조건문 하나로 정책을 뒤집을 수 있는 형태)
```

---

## 3. docs/server-component-design.md

### 3-1. §5-8 HandleRecvPacket() — 수정 (1231-1233줄)

**수정 전:**

```markdown
  CHAT_MESSAGE
    → 같은 packet의 header.nickname을 ECHO_NICK으로 덮어쓴 뒤 SendPacket(packet) (echo)
    → 송신 실패 시 HandleTransportException(send_state)
```

**수정 후:**

```markdown
  CHAT_MESSAGE
    → Manager_wp.lock() 성공 시 manager->broadcast(packet, session_id) 위임
    → broadcast() 내부에서 closing 세션 / 발신자 자신(session_id) 제외 후 각 대상 세션의 SendQueuePush() 호출
    → 헤더의 nickname은 수정하지 않고 수신 당시(발신자의) 값을 그대로 전달
```

### 3-2. §7-3 GetClients() 선언/구현 — 수정 (1727-1738줄)

**수정 전:**

```cpp
std::unordered_map<SessionID, std::shared_ptr<ClientSession>> GetClients();
```

`GetClients()`는 현재 `clients` 컨테이너의 snapshot을 반환하는 함수입니다.

```cpp
std::unordered_map<SessionID, std::shared_ptr<ClientSession>> ClientManager::GetClients() {
    std::lock_guard<std::mutex> lock(clients_mutex);
    return clients;
}
```

**수정 후:**

```cpp
std::vector<std::shared_ptr<ClientSession>> GetClients();
```

`GetClients()`는 현재 `clients` 컨테이너의 snapshot을 반환하는 함수입니다.

```cpp
std::vector<std::shared_ptr<ClientSession>> ClientManager::GetClients() {
    std::vector<std::shared_ptr<ClientSession>> snapshot;
    snapshot.reserve(clients.size());

    {
        std::lock_guard<std::mutex> lock(clients_mutex);

        for (const auto& [id, session_ptr] : clients) {
            snapshot.push_back(session_ptr);
        }
    }

    return snapshot;
}
```

### 3-3. §7-3 GetClients() 설계 의도 — 추가 (1758줄 이후)

**추가할 내용:**

```markdown
#### vector를 선택한 이유

`clients`는 `unordered_map<SessionID, shared_ptr<ClientSession>>`이지만, `GetClients()`의 호출부(닉네임 중복 검사, `broadcast()`)는 모두
"SessionID로 특정 세션을 찾는" 용도가 아니라 "전체를 순회"하는 용도로만 snapshot을 사용합니다.
`ClientSession`이 이미 자기 자신의 `session_id`를 값으로 가지고 있어(`GetSessionID()`), map의 key는 순회 목적에서는 불필요한 정보입니다.

따라서 `GetClients()`는 `unordered_map` 전체를 복사하는 대신, `shared_ptr<ClientSession>` 값만 `vector`로 복사합니다.
`snapshot.reserve(clients.size())`로 미리 메모리를 확보해, `clients_mutex`를 잡고 있는 짧은 구간 안에서 재할당이 발생하지 않도록 합니다.
```

### 3-4. §7-4 Broadcast() [Planned] — 수정 (1760-1828줄)

**수정 전:**

```markdown
## 7-4. Broadcast() [Planned]

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

        pSession->SendPacket(message, len, PacketType::CHAT_MESSAGE);
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
```

**수정 후:**

```markdown
## 7-4. broadcast()

```cpp
void broadcast(std::shared_ptr<Packet> p, SessionID sender_id);
```

`broadcast()`는 한 클라이언트가 보낸 메시지를 다른 클라이언트들에게 전달하는 함수입니다.

```text
Client A → Server → Client B
                  → Client C
                  → Client D
```

실제 구조는 다음과 같습니다.

```text
1. GetClients()로 clients 목록의 snapshot(vector<shared_ptr<ClientSession>>)을 얻는다. (clients_mutex는 GetClients() 내부에서 짧게 잡혔다 풀림)
2. snapshot을 순회한다.
3. GetClosing() == true인 세션은 건너뛴다.
4. GetSessionID() == sender_id인 세션(발신자 자신)은 건너뛴다.
5. 나머지 세션의 SendQueuePush(p)를 호출한다.
```

```cpp
void ClientManager::broadcast(std::shared_ptr<Packet> p, const SessionID sender_id) {
    auto snapshot = GetClients();

    for (auto& client_info : snapshot) {
        if (client_info->GetClosing()) {
            continue;
        }
        if (client_info->GetSessionID() == sender_id) {
            continue;
        }

        client_info->SendQueuePush(p);
    }
}
```

이 구조를 사용하는 이유는,
`clients_mutex`를 잡은 상태에서 실제 `send()`를 오래 수행하지 않기 위해서입니다.

`clients_mutex`를 짧게 잡게 된다면, 후일에 발생할 수 있는 데드락 위험과 락 경합을 줄일 수 있습니다.

그리고 `send()`는 block될 수 있으므로,
manager lock을 잡은 상태에서 모든 클라이언트에게 send하면
느린 클라이언트 하나가 전체 서버 흐름에 영향을 줄 수 있습니다.

따라서 `broadcast()`는 clients 목록을 짧게 snapshot으로 복사하고,
실제 송신은 manager lock을 풀고, 해당 세션을 담당하는 `SendRun()`이 수행하는 구조를 취합니다.
`broadcast()`를 호출한 스레드는 `SendQueuePush()`만 수행하고 `send()`를 직접 호출하지 않습니다.

```text
clients_mutex
  → clients 목록 보호

send_queue_mutex
  → 특정 ClientSession의 Send Queue push() / pop() 보호 (Header + Payload 송신 순서 자체는 SendRun() 하나로 송신 주체가 고정되어 구조적으로 보장됨)
```
```

---

## 4. docs/concurrency-design.md

### 4-1. §14 Broadcast snapshot 구조 — 수정 (592-629줄)

**수정 전:**

```markdown
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

        client->SendQueuePush(packet); // 실제 송신은 해당 세션의 SendRun()이 수행
    }
}
```
```

**수정 후:**

```markdown
`ClientManager::broadcast()`는 다음 흐름으로 구현되어 있습니다.

```text
1. GetClients()를 호출해 clients 컨테이너의 snapshot(vector<shared_ptr<ClientSession>>)을 얻는다. (clients_mutex는 GetClients() 내부에서 짧게 잡혔다 풀림)
2. snapshot을 순회한다.
3. GetClosing() == true인 세션은 건너뛴다.
4. GetSessionID() == sender_id인 세션(발신자 자신)은 건너뛴다.
5. 나머지 세션의 SendQueuePush(p)를 호출한다.
```

실제 구조:

```cpp
void ClientManager::broadcast(std::shared_ptr<Packet> p, const SessionID sender_id) {
    auto snapshot = GetClients();

    for (auto& client_info : snapshot) {
        if (client_info->GetClosing()) {
            continue;
        }
        if (client_info->GetSessionID() == sender_id) {
            continue;
        }

        client_info->SendQueuePush(p); // 실제 송신은 해당 세션의 SendRun()이 수행
    }
}
```
```

(이어지는 "이 구조의 장점은 다음과 같습니다" 이하 목록은 수정 없이 그대로 유지)

### 4-2. §16 아직 남은 정책 결정 — 항목 삭제 (863줄)

**삭제할 내용:**

```markdown
- sender 자신에게도 메시지를 보낼 것인가?
```

---

## 5. docs/protocol.md

### 5-1. §2 특수 닉네임 상수 — 추가 (327줄 이후)

**추가할 내용:**

```markdown
브로드캐스트 도입 이후 `CHAT_MESSAGE`는 `ClientManager::broadcast()`로 위임되며, 헤더의 발신자 닉네임을 덮어쓰지 않고 수신 당시의 값을 그대로 전달합니다.
따라서 `ECHO_NICK`은 현재 코드 내에서 실제로 사용되는 곳이 없습니다. 상수 자체는 향후 다른 서버 생성 패킷에서 재사용될 가능성을 고려해 유지합니다.
```

### 5-2. §6 송수신 순서(서버 기준 처리) — 수정 (477줄)

**수정 전:**

```markdown
    CHAT_MESSAGE          → 같은 packet에 ECHO_NICK을 덮어써서 그대로 SendPacket(packet) (echo)
```

**수정 후:**

```markdown
    CHAT_MESSAGE          → ClientManager::broadcast(packet, session_id) 위임 (closing 세션 / 발신자 자신 제외, 헤더의 nickname은 수정하지 않고 그대로 전달)
```

---

## 6. docs/roadmap.md

### 6-1. §3 현재 기준 미구현(Others) — 항목 삭제 (203줄, 208줄)

**삭제할 내용:**

```markdown
- Broadcast Chat 기능
```

```markdown
- clients snapshot 기반 broadcast 구조
```

### 6-2. §2 구현 완료 — 새 섹션 추가

**위치:** "### Send Queue, Send / recv Thread 기반 서버 로직 개편" 섹션(180줄) 뒤

**추가할 내용:**

```markdown
### Broadcast 도입

- `ClientManager::broadcast(std::shared_ptr<Packet> p, SessionID sender_id)` 구현
- `ClientManager::GetClients()`를 `unordered_map` 반환에서 `vector<shared_ptr<ClientSession>>` 반환으로 개편 (`reserve()` 기반 재할당 최소화)
- `ClientSession::HandleRecvPacket()`의 `CHAT_MESSAGE` 처리를 자기 자신 echo에서 `ClientManager::broadcast()` 위임으로 전환
- 송신 정책: `closing == true` 세션 제외, 발신자 자신 제외 (조건문 하나로 정책 전환 가능한 형태 유지)
```

### 6-3. §4 단기 구현 목표 — 섹션 삭제 (351-371줄)

**삭제할 내용:** "### 4-4. ClientManager::Broadcast() 구현" 섹션 전체 (구현 완료되어 §2로 이동)

```markdown
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
```

### 6-4. §5-1 Broadcast Chat Server(결정할 정책) — 항목 삭제 (388줄, 391줄)

**삭제할 내용:**

```markdown
- sender 자신에게도 메시지를 보낼 것인가?
```

```markdown
- `closing == true` 세션을 snapshot 단계에서 제외할 것인가, 순회 단계에서 제외할 것인가?
```

(둘 다 결정됨: 발신자 제외 / 순회 단계에서 `GetClosing()`으로 제외)

### 6-5. §9 현재 우선순위 — 수정 (556-559줄)

**수정 전:**

```text
1. LineLogger 프로젝트 전면 적용 (서버 전역 로그, ClientManager 로그 교체)
2. send_mutex 추가
3. ClientManager::Broadcast() 구현
4. Chat Server로 확장
```

**수정 후:**

```text
1. LineLogger 프로젝트 전면 적용 (서버 전역 로그, ClientManager 로그 교체)
2. send_mutex 추가
3. Chat Server로 확장
```

---

## 7. docs/client-component-design.md

### 7-1. §3-5 WriteChatLog() 사용 예시 — 추가 (316줄 이후)

**추가할 내용:**

```markdown
`WriteInputLog()`는 `CHAT_MESSAGE` 수신 시뿐 아니라, `NICKNAME_CHANGE_FAILED` / `NICKNAME_CHANGE_SUCESS` 응답을 출력한 직후에도 호출됩니다.
서버로부터 온 응답이 콘솔에 출력될 때마다 입력 프롬프트를 다시 그려주기 위한 것으로, 세 경우 모두 동일한 목적(프롬프트 재출력)을 가집니다.

```cpp
// NICKNAME_CHANGE_FAILED / NICKNAME_CHANGE_SUCESS 수신 시
LineLogger::GetInstance().WriteChatLog(res.nick, ...);
LineLogger::GetInstance().WriteInputLog("Message to send (Maximum 4096 Bytes) : ");
```
```
