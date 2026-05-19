# TODO

### 지금 당장 해야할 것
- `SendPacket()` / `RecvPacket()` 분리 - 현재는 선언은 해놨음
- `SessionID` 설계 및 구현
-  로그 출력 개선

### 적당히 빨리 해야할 것
- 없음

### 천천히 해도 좋은 것
- C++ 공부 후에 본격 시작 예정

### Visual Studio에서 쉽게 볼 수 있는 로드맵
1. SessionID 설계 + 구현
2. 로그 출력 개선
3. clients 컨테이너 임시 결정
4. std::mutex / std::lock_guard / std::unique_lock / std::atomic 기본 공부
5. SendPacket / RecvPacket 분리
6. ClientSession별 send_mutex 적용
7. Broadcast MVP 구현
8. clients 컨테이너 최종 결정 및 교체
9. condition_variable 공부
10. 자료구조 몇 개 구현해보기
11. lock-free stack 가볍게 구현해보기
12. send queue / worker thread 구조 검토