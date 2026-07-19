# TODO

### 지금 당장 해야할 것

- 클로드가 코드리뷰해준 것 기반으로 지금 고칠 수 있는거 고쳐보기
- 이제 브로드캐스트 드가야할 듯.. 시발
- BROADCAST!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  - 설계 (미완)
  - 구현 (미완)
  - README(남들이 읽을 수 있는 문서)화 (미완)

1. 브로드캐스트 1단계 : `Packet` 구조체 관련 개편
2. 브로드캐스트 2단계 : `Send Queue`, send / recv thread 관련 개편
3. 브로드캐스트 3단계 : `Broadcast()` 관련 개편

셋다 뭔가 연관 없어보이지?
의외로 ㅈㄴ 많음.

의존성 적어놓은거(현재는 비어있음) 기반으로
각 단계마다

- 각각 구현하고
- 각각 문서화하셈

ㅇㅋ?

설계는 솔직히 같이 하고..
(솔직히 이건 서로 얽혀있는거라..)

- `Broadcast()` : 1 send 1 recv 보장 안됨 -> Send 할걸 쌓아놓을 `Send Queue`와 별도의 recv / send thread를 따로 파서 돌려야함..
- `Send queue`, recv / send thread : 먼저 해당 `Send Queue`에 넣을 단위인 `Packet` 기반으로 송 / 수신 함수 갈아엎어야함. recv / send thread는 별도임.
- `Packet` : 이건 다른 작업보다 먼저 진행 쌉가능함.

ㅇㅋ? ㅇㅋ? ㅇㅋ?
제발 이대로 해..

### 적당히 빨리 해야할 것

### 천천히 해도 좋은 것

- 서버 로그 세분화(각 작업마다 따로 로그 찍기. 단순 PacketSent / PacketRecv만으로는 현재 실행중인 작업과 해당 작업의 결과를 나타내기에는 부적합함. 패킷 핸들러에 놓는게 좋을 듯.)
- const로 바꿀 수 있는 (함수의) 매개변수 / 반환값 전부 const로 바꿔서 읽기 전용 매개변수 / 반환값이라는거 명시하기.
- C++ 공부 후에 본격 시작 예정

### Visual Studio에서 쉽게 볼 수 있는 로드맵

1. SessionID 설계 + 구현 (1차 완료)
2. 로그 출력 개선 (완료)
3. clients 컨테이너 임시 결정 (완료)
4. std::mutex / std::lock_guard / std::unique_lock / std::atomic 기본 공부 (완료)
5. SendPacket / RecvPacket 분리 (완료)
6. ClientSession별 send_mutex 적용
7. Broadcast MVP 구현
8. clients 컨테이너 최종 결정 및 교체
9. condition_variable 공부
10. 자료구조 몇 개 구현해보기
11. lock-free stack 가볍게 구현해보기
12. send queue / worker thread 구조 검토