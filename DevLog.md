# 개발 로그

### 26.04.19

- 서버의 기본적인 뼈대 세팅
  - 기본적인 상수들 초기화
  - WSAStartup() - 윈속 초기화
  - socket() - listen용 소켓 생성
  - 서버의 listen용 소켓의 소켓 주소 구조체 생성  
  - bind() - listen용 소켓에 소켓 주소 구조체를 바인딩
  - listen() - listen용 소켓의 상태를 LISTEN(LISTENING)으로 변경(accept() 준비 완료)
  - while루프 - accept() 완료
  - 기본적인 객체들 기본 설정
	- ClientInfo 구조체 - 현재로써는 완료
	- Clientmanager 클래스 - 컨테이너 라이브러리들 제대로 배운 후 본격적으로 시작할 예정
  - client_thread 단순 선언만 함
  - 클라이언트는 나중에 뼈대 세팅할 예정

### 26.05.02

- 서버 코드에 새로 배운 shared_ptr / weak_ptr / unique_ptr(의 뼈대) 추가
  - 자원과 객체의 소유권 관점
  - unique_ptr은 아직 배우지 않음
    - 배운 후에 제대로 갈아엎을 예정.
  - ClientManager 클래스
	- 클라이언트들을 관리할 클래스
	- 채팅 서버의 브로드 캐스트 등의 기능을 고려해서 미리 설계
	- 일단 명시적으로 복사 방지 코드 넣어놓음. 공유 컨테이너 때문에 복사하면 안됨..
	- public std::enable_shared_from_this
	- std::vector<std::shared_ptr<ClientSession>> clients - 클라이언트들의 정보(shared_ptr) 들을 소유할 ClientManager 클래스의 컨테이너. 
	  - 스택 영역의 객체인지 힙 영역의 객체인지 추가적인 고려 필요.
	  - 나중에 추가로 다른 컨테이너도 고려해볼 예정
	- std::mutex client_mutex - clients 공유 컨테이너에 접근할 때 사용하는 lock.
    - void AddClient(std::shared_ptr<ClientSession> client) - clients에 client_mutex를 사용한 lock과 함께 클라이언트의 shared_ptr을 push_back()
	  - std::lock_guard는 지금 상태에선 로그에는 못 쓰겠다.. 주석 참고
      - shared_from_this()로 control block을 고려해서 ClientManager 객체의 shared_ptr을 ClientSession 객체에 전달
  - ClientSession 클래스
	- 클라이언트들의 각 세션을 표현할, 각 세션의 정보를 담고있는 클래스
	- 일단 명시적으로 복사 방지 코드 넣어놓음. 어차피 이 객체는 client_thread 함수에 move semanstic으로 넘겨줄 생각.. 이었는데
	  - 그냥 ClientManager 클래스도 ClientSession을 소유할 필요가 있으므로 shared_ptr로 넘길 듯..
	- std::unique_ptr<ClientSocket> sock - raw socket의 정보와 raw socket으로 가능한 행동(send / recv 등)이 들어있는 ClientSocket 객체. 
	  - unique_ptr로 해당 ClientSocket 객체를 독점적으로 소유한다고 명시.
	- sockaddr_in ClientAddr - ClientSocket(sock) 객체의 SOCKET의 주소가 저장되어있는 구조체. 
	  - 이건 나중에 ClientSocket 객체 내부로 옮기는 것도 고려해봐야 할듯.
	- std::weak_ptr<ClientManager> Manager_wp - ClientManager 객체의 shared_ptr로부터 받을 weak_ptr. 
	  - cyclic reference를 막고 ClientManager 객체의 lifetime에 간섭하면 안되는 하위 객체이기 때문에 weak_ptr을 씀.
	  - 객체 생성 시점에서는 ClientManager 객체의 shared_ptr을 받는 구조가 아니므로, shared_ptr을 복사하는 것으로 초기화는 하지 않음.
    - ClientSession(std::unique_ptr<ClientSocket> s, sockaddr_in addr) -  ClientSession 객체의 생성자. 
	  - unique_ptr로, ClientSocket 객체를 이동(move)이라는 semantic으로, sock으로 받아오기.
      - 물론 해당 ClientSocket 객체의 소켓 주소 구조체도 받아서 ClientSession 객체의 ClientAddr으로 받아오기.
		- 이건 단순 복사로 처리해도 상관없음. 지금은. 그래도 복사할 때의 오버헤드가 있긴 하고 재할당을 해야하니(레퍼런스 불가) unique_ptr로 받아오는 것으로 개선해도 뭔가 좋을 것 같음.
		- 아님. sockaddr_in은 소켓 주소 정보를 담는 값 데이터에 가깝기 때문에, 일단은 단순 복사로 처리해도 충분해 보임.
        - 나중에 IPv6까지 고려한다면 sockaddr_storage로 확장하는 것도 고려할 수 있을 것 같음.
    - void AddToManager(std::shared_ptr<ClientManager> Manager_sp) - ClientManager가 사용하는 ClientManager 객체의 shared_ptr을 넘겨주는 함수.
	  - shared_ptr(Manager_sp)를 받아서 weak_ptr에 복사해서 weak_ptr(Manager_wp) 초기화? 
		- 이거 초기화 맞나? 다음에 알아보자. 지금은 피곤해..
    - 소멸자에선 manager_wp.lock()으로 ClientManager 객체가 살아있는지 확인 후 반환된 shared_ptr로 ClientManager의 공유 컨테이너에서 해당 클라이언트 제거하는 함수 호출
	  - ..인데 이건 나중에 다시 봐야할 듯. 재검토 필요. 
- 추가적인 수정은 unique_ptr 공부한 후에 할 예정.
- 시발 내가 지금 뭘 한거지 시발 ㅈ됐다 못 쉬고 이런거 적어버렸다 시발 번아웃 상태인데 시발 진짜 ㅈ됐다

### 26.05.04

- 각 객체 간의 관계 설계
  - IdeaScatch 참조
- `unique_ptr`, `shared_ptr` 관련 Syntax Error 해결
  - `std::move()`함수에 대한 이해 부족이 원인.
    - 생성자의 인자로 `unique_ptr<ClientSocket>`을 move를 이용해 받아왔지만, 객체 내부에 `unique_ptr<ClientSocket>`인 ClientSock에 소유권을 이전할 때 `std::move()` 함수를 사용하지 않았음.
	- 생성자 내부에서도 `std::move()` 함수로 ClientSock에 소유권을 이동시켜서 해결.
  - 추가적으로, `main()` 함수의 `shared_ptr<ClientSession>`을 생성하는 부분에서 오류 발생.
	- 초기화가 문제였음. 단순히 `shared_ptr<ClientSession> client(std::move(client_socket), client_addr)`로 잘못된 방식이었음.(생성자인줄 알고 생성자처럼 인자를 넣어버림)
	- `std::make_shared()` 함수로 초기화하여 해결.
- 서버 코드에 IdeaScatch에서 설계한 ClientSession을 직접 코드로 구현
  - ClientSession 
    - 클라이언트 하나의 연결 단위
	- 해당 클라이언트의 ClientSocket 소유(`unique_ptr`)
	- 해당 클라이언트의 주소, 송수신 상태 저장(단순 값)
	- 필요시 ClientManager 참조(소유하지 않음. `weak_ptr`)
	- 1 client_thread() <-> 1 ClientSession 구조
	- private 접근 지정자
	  - `unique_ptr<ClientSocket>` ClientSock 
		- raw SOCKET을 객체로 감싼 RAII 객체의 `unique_ptr`. 
		- `unique_ptr`을 사용해서 해당 클라이언트의 ClientSession 객체만 해당 클라이언트의 `ClientSocket` 객체를 소유할 수 있음을 명시.
		- send_all(), recv_all()은 이 객체가 함, 복사 불가, 단독 소유.
		- 이건 이미 지난번에 구현해놓음.
      - `sockaddr_in` ClientAddr 
		- 클라이언트 IP 주소를 저장할 소켓 주소 구조체. 
		- 이것도 이미 지난번에 구현해놓음.
      - `weak_ptr<ClientManager>` Manager_wp 
		- ClientManager 소유하지 않고 참조만 할 용도의 `weak_ptr`.
		- 이것도 이미 지난번에 구현해놓음.
      - `NetState` ClientState 
		- 송 / 수신의 현재 과정과 예외 상황을 기록할 상태 관리 구조체.
		- `ClientSocket`이 `ClientState&`를 멤버로 들고 있을 필요는 없음.
		- `ClientSession`이 `ClientState`를 값으로 소유하고, 송 / 수신 시 `ClientSockSend()` / `ClientSockRecv()`에 레퍼런스로 넘겨 상태 변화를 반영.
    - public 접근 지정자
	  - ClientSession() - 생성자
		- 받은 `unique_ptr<ClientSocket> s`를 `std::move(s)`로 멤버 `ClientSock`에 이동시켜 초기화.
		- ClientAddr은 값 복사로 받아서 초기화. - 간단한 값일 뿐이므로. (소유권 상관없음)
		- ClientState는 모든 필드 0(false)로 초기화. - 혹시 모를 쓰레기값이 있을까봐.
	  - void AddToManager(shared_ptr<ClientManager> manager_sp)
		- `shared_ptr<ClientManager>`를 받아서 Manager_wp를 초기화하는 함수
	  - void Run()
		- 이 함수는 아예 갈아엎음.
		- 원래는 송 / 수신 함수를 분리할 예정이었지만, ClientState를 보고 각 에러에 맞는 메시지를 출력, 에러 메시지를 클라이언트에 전송하는 과정을 넣기 어려울 것 같아서 Run() 함수로 송 / 수신 과정을 통합.
		- 이전의 에코 서버와 로직 변경 없음. 기존의 에코 서버의 송 / 수신 코드 사실상 복붙함.
		- 딱 변수 이름 + 포인터 역참조 변경밖에 변경점 없음.
		- 이거 하다가 토할뻔 함.
		- 나중에 구조가 안정된다면 `RecvPacket()`, `SendPacket()`, `SendHeaderErrorPacket()` 같은 함수로 분리할 수 있음.

## 26.05.08 ~ 26.05.09

- main 스레드와 client_thread 스레드 간의 흐름 설계
  - IdeaScatch 참조
- ClientSession 객체 추가 설계
  - IdeaScatch의 ClientSession 설계도(4) 참조
- 서버 코드에 IdeaScatch에서 설계한 스레드 간의 흐름을 직접 코드로 구현
  - `ClientThread` (`client_thread()` 함수)  
	- `std::thread` `ClientThread` : 생성 직후 `detach()`
	  - `join()`으로 구조 짜는건 지금 당장은 도저히 못하겠음. 
	  - 그래서 지금은 보다 간단한 `detach()`로 `std::thread`와 실제 스레드를 분리하고
	  - 해당 분리된 스레드의 상태는 `std::thread` 객체로는 기록될 수 없으므로 `ClientSession` 객체에 기록.
		- `client_thread`의 종료는 `ClientSession` 객체에 `atomic<bool> closing` 변수로 기록.
		  - `ClientManager` 객체가 추후에 broadcast로 `SendPacket()`를 수행할 때 `closing`이 `true`인 `ClientSession`에는 `SendPacket()`을 수행하지 않는 식으로 최적화.
		  - `closing`에는 `std::atomic`을 사용해서 `closing`을 변경할 때 데이터 레이스가 발생하지 않게 동기화를 하도록 설정해서 `ClientManager` 객체가 확실한 `closing`여부를 알 수 있게 함.
		  - 변수 하나만 동기화를 해주면 되고 상대적으로 자주 수행될 수 있는 연산이므로 상대적으로 오버헤드가 적은 `std::atomic` 선택.
    - `Run()` 함수 실행 도중 클라이언트 세션을 종료해야하는 상황(현재는 `error`/ `peer_exit` 정도) 발생 시 `ClientSession` 객체의 `closing`을 `true`로 변경하기
	- `Run()` 함수 실행 도중 `ClientSession` 객체의 `closing`을 `true`로 바꾼 후 `RemoveThisClient()` 함수 호출.
	- `RemoveThisClient()` 함수는 `Manager_wp`로 `ClientManager` 객체의 `RemoveClient()` 함수를 실행함.
	- `void ClientManager::RemoveClient(shared_ptr<ClientSession>)` 함수는 `ClientSession` 객체를 인자로 받아서 `lock_guard<mutex> lock(client_mutex)`로 락을 건 후 해당 객체를 `clients`에서 삭제하는 함수.
	  - 미리 임시로 구현해놓음. 
	  - 나중에 `ClientManager` 클래스를 제대로 설계한 후 제대로 구현해볼 예정.
- 변경된 구조로 인한 ClientSession 객체에 `std::atomic<bool>` `closing` 추가. `false`로 기본값 설정.(시작부터 `true`면 `ClientManager`가 오인하고 `SendPacket()` 하지 않을 수 있음.)
- `ClientSession` 클래스의 가독성을 위해 `Run()` 함수 선언부와 구현부 분리.
- shared_ptr = 물리적 생존 보장
- closing = 논리적 종료 상태
- RemoveClient = 관리 목록에서 제거

### 26.05.19 

- 밀린 로그 정리
  - 전체적인 각 스레드 / 객체의 책임, 역할 설계 - IdeaScatch 참조
  - 클라이언트 코드 - 기존 Echo Server 코드의 클라이언트 코드 복붙
  - 실제 실행 - [velog](https://velog.io/@siryus0907/MultiClient-Echo-26.05.18%EC%9B%94-%EB%A9%80%ED%8B%B0%EC%8A%A4%EB%A0%88%EB%93%9C-%EB%A9%80%ED%8B%B0%ED%81%B4%EB%9D%BC%EC%9D%B4%EC%96%B8%ED%8A%B8-%EC%97%90%EC%BD%94%EC%84%9C%EB%B2%84%EC%9D%98-%EC%8B%A4%ED%96%89-%EA%B0%9C%EC%84%A0%EC%A0%90)
    - 이상 없이 잘 의도한 대로 실행됨
	- 개선점
	  - ClientSession을 더 잘 구분할 수 있도록 SessionID가 필요함.
	  - 서버의 로그가 좀 더 세부적이어야 함 - 현재는 송 / 수신 시에만 출력함. 클라이언트 접속 / 접속 종료 시에도 출력해야 할 것 같음.

### 26.05.20

- `ClientSession::Run()` 함수 리팩토링
  - `SendPacket()` / `RecvPacket()` 함수로 분리.
  - 기존의 `Run()` 함수는 `SendPacket()` / `RecvPacket()` 함수를 이용한 Echo 로직.
  - `SendPacket()` 함수
	- 기존의 송신 과정과 다를바 없음.
	  - 헤더 만들고(`htonl()` 포함), 헤더 송신, 페이로드 송신.
  - `RecvPacket()` 함수
	- 기존의 수신 과정과 다를바 없음.
	  - 헤더 해석하고, 헤더의 `length` 필드에 맞춰서 페이로드 수신.
- protocol error 정책 정립
  - `PacketHeader`의 `length` 필드가 0이어도 protocol error로 정책을 정립
- enum class 도입
  - 버그 ㅈㄴ 많았음.
	- 일단 처음에는 단순히 `PacketHeader`의 `type` 필드에도 `enum class PacketType`을 썼는데,
	- 의미가 잘 안 맞아서
	- `type` 필드에는 `int32_t`의 정수 데이터를 넣고,
	- 판단은 `enum class PacketType`을 보고 판단을 하는 식으로..
	- 근데 `enum class`의 깐깐한 암시적 형변환 봉쇄 + 바이트 정렬 변환 함수의 인자 조건(숫자여야함)이 시너지를 일으켜서
	- 그냥 계속 모든 `PacketType`을 사용하는 부분에 에러가..ㅋㅋ

### 26.05.22

- `SessionID` 설계 및 구현
- 에라모르겠다 던져 미래의 내가 하겠지..
- 1. SessionID 부여 규칙 설계
- SessionID의 목적 : 
클라이언트 세션들을 구분해서 매니저가 인식할 수 있도록. 
IP:Port만으로는 부족해. 
그리고 매니저가 shared_ptr 대조가 아니라 
세션 아이디로 바로바로 RemoveClient() 할 세션을 찾을 수 있도록.
어쨌든 **Manager의 Session관리 편의성**을 위해서. 
- SessionID에 대한 브레인스토밍 :
- 1. 세션 ID 부여 규칙이 필요하다. 
    - 이거 어떻게 할까?
    - 뭔가 알겠다. 이거 `accept()` 직후에 붙여야 할 것 같음.
    - 그리고 클라이언트 세션 ID를 부여하는건.. 말보단 코드가 더 쉽겠다.
    ```cpp
    // main() 스레드 내부, accept() 루프 외부
    using SessionID = uint64_t; // 이거 이렇게 하는거 아닌가? 모르겠다. 그래도 의도는 충분히 설명됨.
    int CUR_SESSION_ID = 0;

    while (true) {
        // 대충 accept() 해서 Manager에 AddClient()하기 직전
        AddClient(CUR_SESSION_ID, client); // client는 생성된 ClientSession 객체
        CUR_SESSION_ID += 1; // 세션 ID 부여하고 다음 세션 ID로 설정, 선형으로 세션 ID가 증가하므로 오버플로우 아니면 중복 세션 ID 불가..
    }
    ```
    - 이렇게 하면 여러 클라이언트가 동시에 접속해도 main() 스레드 내부에서만 세션 ID 부여가 일어나므로 
    세션 ID를 결정하는 변수의 레이스 컨디션을 걱정할 필요가 없어진다.
    - 그리고 세션 ID는 코드처럼 64비트짜리 unsigned int(64비트 부호없는 정수) 자료형을 사용한다.
    솔직히 이 세션 ID가 전부 고갈날 일이 있을까 싶다. 
    $UMax = 2^64 - 1$.. $2^64 = 2^31 \times 2^31$.. 42억 곱하기 42억.. 이건 절대 못넘어.. 클라이언트 세션 백억개든 천억개든 생성되어도.. 
    - 근데 32비트는 42억개라 오버플로우가 발생할 가능성이 조금은 있긴 함.. 그래서 그냥 안전빵으로 uint64_t 씀..
    - 그리고 ClientManger의 clients도 unordered_map으로 바꿀 시기인 듯.. 삽입 / 삭제도 빠르고, key-value 구조고..
    - 근데 이게 사실상 세션 ID 설계 끝 아님? 솔직히..
    - 아참참 ClientManager::RemoveClient()도 SessionID 기반으로 갈아엎을 수 있음. 
    그러면 ClientSession::RemoveThisClient()도 갈아야겠네.. 여기 이제 shared_from_this() 쓸 필요 없겠네.. 근데 나중에 필요할 수도 있으니까 고민좀해보고 빼자.
    ClientSession의 shared_ptr을 넘기지 않아도, 그냥 SessionID만 넘겨도 되니까..
- 2. 세션 ID로 각 클라이언트 세션을 구분한다. 
    - ㅇㅇ. 이게 목적이지.
    - ClientManager의 clients 컨테이너는 나중에 unordered_map으로 바꾸면, Manager가 ClientSession을 식별하는데는 key인 세션 ID를 쓰고,
    - ClientSession이 각 클라이언트의 세션 ID를 판별할 때는 ClientSesssion 내부의 쓰는 식으로.. (생성자에서 초기화) 
      - 그래서 ClientSession에 SessionID 멤버 추가해야함.

### 26.5.30

- (밀린 로그 작성)
- `LineLogger` 로깅용 싱글톤 객체 추가(로그 출력 개선)
  - 핵심 목적 : 로그 한 줄의 의미 깨짐 방지
  - `std::cout` 출력 스트림(공유 자원)에 대한 동기화 지원
	- 데드락 위험과 락 경합을 줄이기 위해서 `std::cout`를 사용할 때만 mutex 획득.
  - 해당 객체에 동일한 mutex를 사용해서 동기화하기 위해선 모든 로그 출력이 같은 mutex를 공유해야하므로 싱글톤 패턴 적용
  - 문자열을 하나로 합쳐서 출력함으로써 여러 `<<` 연산자를 쓰지 않아도 되게 개편.
	- 여러번의 `<<` 연산자로 인한 오버헤드 감소 목적.
  - 가변 인자 템플릿을 적용하여 `LineLogger` 객체의 로그 출력 함수(`WriteLog()` 등)에 몇 개의 인자가 들어오더라도 처리할 수 있게 함.
  - `Session`레벨 로그 형식을 자동으로 잡아주는 인터페이스 느낌의 `WriteSessionLog()`함수 추가
	- 추후 다른 레벨의 로그를 추가할 때 다른 인터페이스 느낌의 함수를 추가할 수 있음.
  - 따로 `enum class` `LogType`으로 유효한 로그 타입을 모아놓아서 프로그래머의 실수를 방지.
  - 해당 객체를 사용하여 로그를 출력하도록 `Session` 레벨의 `std::cout`를 전부 수정
	- 각각의 로그 타입을 알맞게 배치
- `TransportExceptionHandling()` 함수가 어떤 과정의 후처리를 담당하는지 명확히 하기 위해서 `NetState` 구조체를 인자로 받아 해당 구조체를 보고 후처리를 하도록 개선
  - 기존에는 `NetState` `ClientState`를 참조해서 예외처리를 해서 어떤 과정의 후처리를 담당하는지 명확하지 않고, 예외 상황 발생과 해당 함수의 호출 시점이 달랐기 때문에 그 사이에 `ClientState`가 갱신된다면 후처리에 문제가 발생할 수 있었음
  - 기존의 `SendPacket()` / `RecvPacket()`도 그 당시의 `ClientSession` 객체를 반환했지만, 위와 같은 이유로 명확히 예외 상태가 기록되지 않을 위험이 있었음. 
	- 그래서 내부 지역 객체인 `send_packet_state` / `recv_packet_state`에 해당 함수들의 송 / 수신 상태를 기록하고, 해당 객체를 반환함으로써 `TransportExceptionHandling()` 함수의 목적인 "어떤 과정의 후처리를 담당하는지 명확히 한다"라는 의도와 일치시킴.

### 26.06.07

- (밀린 로그 작성)
- `InputParser` / `ParsedInput` 추가
  - 핵심 목적 : 클라이언트가 입력을 받을 때 해당 입력으로 어떤 처리를 해야하는지 입력을 파싱 (기존 구조에서는 일반 채팅 메시지로밖에 처리 못했음)
  - `InputParser::Parse()` 함수로 입력 파싱, `ParsedInput` 구조체 반환 : `static`으로 객체를 생성하지 않고 해당 함수를 호출할 수 있게 해서 편의성 챙김
	- `ParsedInput`에는 어떤 동작을 해야하는지 기록되어있으므로 `ParsedInput`을 보고 동작 처리
	  - `valid` -> `quit` -> (기타 패킷을 전송하지 않는 명령어) -> `type` 순으로 검사해야함.
  - 자세한건 [링크](./IdeaScatch/26.06.04 Inputparser 구조체 설계.md)
- `ClientApp` 클라이언트 객체 추가
  - 핵심 목적 : 클라이언트의 정보 / 동작을 한 객체에 묶어놓아서 정보 관리의 편의성 / 안전성, 동작의 편의성을 챙기기
  - `ConnectSock` / `NetState` / `Nickname` 해당 객체가 소유
  - `SendPacket()` / `RecvPacket()` / `Run()` / `HandleTransportException()` 함수 제공
  - 자세한건 [링크](./IdeaScatch/26.06.05 ClientApp 설계.md) 참조
	- 다시 쓰기 귀찮음..

### 26.06.12

- (밀린 로그 작성)
- 패킷 처리 정책 명확화
  - 핵심 목적 : 기존의 `HEADER_ERROR`도 `NetState`에 기록해서 
  후처리 함수(`HandleTransportException()`)에서 처리했기 때문에 
  패킷 처리의 책임 경계가 애매해서 패킷 핸들러 설계가 어려워서
  명확히 패킷 처리의 책임을 분리해야했음
	- 기존의 "종료해야 하는 패킷 / 종료하지 않는 패킷" 기준 대신,
    "수신 과정에서 예외가 발생한 경우"와
    "정상적으로 수신된 패킷"을 기준으로
    패킷 처리 책임을 분리함
	- 기존의 `RecvPacket()` 함수는 오직 `NetState`와 페이로드만 반환했기에, 패킷 타입까지 제대로 알기 위해서 `RecvResult` 구조체를 반환하도록 개편
	- 패킷 수신 과정에서 예외 상황 발생
 	  - `NetState`에 기록 및 `NetState`만 보고 처리
	  - `HandleTransportException()`에서 처리
	  - `peer closed` 상태(`recv() == 0`)도 패킷 수신 과정에서의 예외 상황이니 여기서 처리
	- 정상적으로 패킷 수신
	  - `RecvResult` 전체를 봄
	  - `HandleRecvPacket()`에서 처리
	  - `PacketType::HEADER_ERROR`도 종료해야하는 상황이긴 하지만 정상적인 패킷이니 여기서 처리
  - 자세한건 [여기서](./IdeaScatch/26.06.09 대개편.md)
- 패킷 핸들러 도입
  - 패킷 타입을 보고 해당 패킷 타입에 알맞는 처리를 하는 `HandleRecvPacket()` 함수 추가
  - 자세한건 [여기서](./IdeaScatch/26.06.09 패킷 핸들러 설계.md)
- 이 변경 사항들은 서버와 클라이언트 모두에 적용됨

### 26.06.27

- (밀린 로그 작성)
- 닉네임 시스템 적용
  - 핵심 목적 : 사용자 입장에서의 각 클라이언트 식별
  - 자세한거 도저히 못쓰겠다.. 이걸 어떻게 요약해야해?
    - 결국 프로토콜 수정, `PacketHeader::nickname` 필드 패딩 / 파싱 절차 적용,
    - 패킷 핸들러(`HandleRecvPacket()`)에 닉네임 시스템 관련 추가
	- 특수 닉네임 상수 추가
    - 이 정도지 않을까?
  - 자세한건 [여기서](./IdeaScatch/26.06.22%20닉네임%20시스템%20설계.md)
- 이거 존나 힘들어서 1주일이나 써버렸네ㅋㅋ 2주 통으로 증발ㅋㅋ(1주 동시성 딥하게 파기 / 1주 닉네임 시스템 + 문서화 프롬프트 짜기)
- 문서화 프롬프트 짠거 올리고는 싶은데 무서워서 그냥 나만 가지고 있어야겠다.. 
  - 사실 이게 제일 힘들었음ㅋㅋ

### 26.07.19

- 얼마만의 개발로그냐ㅋㅋ
- `MarkClosing()` -> `TryMarkClosing()` 개편
  - 핵심 목적 : double close 방지
  - 자세한거 도저히 못쓰겠다.. 뇌 용량 초과!
  - 자세한건 [여기서](./IdeaScatch/26.07.19%20MarkClosing%20개편.md)
- 이제 좀 달려보자.. 다음에는 `Packet` 구조체 기반 리팩토링임..