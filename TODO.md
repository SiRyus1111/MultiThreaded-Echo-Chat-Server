# TODO

### 지금 당장 해야할 것

- 클로드가 코드리뷰해준 것 기반으로 지금 고칠 수 있는거 고쳐보기
- 이제 브로드캐스트 드가야할 듯.. 시발
- 

### 적당히 빨리 해야할 것

### 천천히 해도 좋은 것

- 서버 로그 세분화(각 작업마다 따로 로그 찍기. 단순 PacketSent / PacketRecv만으로는 현재 실행중인 작업과 해당 작업의 결과를 나타내기에는 부적합함. 패킷 핸들러에 놓는게 좋을 듯.)
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