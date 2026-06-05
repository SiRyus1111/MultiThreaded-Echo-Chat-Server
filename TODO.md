# TODO

### 지금 당장 해야할 것

- `NetState` 구조체 이름 갈아엎기 (완료)
- 클라이언트 리팩토링
  - `LineLogger` 적용
  - `InputParser` / `ParsedInput` 기반 입력 파싱 시스템 설계 및 구현
  - `ClientApp` 기반 클라이언트 정보 및 동작 시스템 설계 및 구현
- `LineLogger` 구조체에 클라이언트 리팩토링 과정에서 필요해진 입력 로그 인터페이스 함수 / 클라이언트 로그 전용 인터페이스 함수 추가

### 적당히 빨리 해야할 것

- 변수 / 함수 이름 좀 손보기

### 천천히 해도 좋은 것

- C++ 공부 후에 본격 시작 예정

### Visual Studio에서 쉽게 볼 수 있는 로드맵

1. SessionID 설계 + 구현 (1차 완료)
2. 로그 출력 개선 (완료)
3. clients 컨테이너 임시 결정 (완료)
4. std::mutex / std::lock_guard / std::unique_lock / std::atomic 기본 공부
5. SendPacket / RecvPacket 분리 (완료)
6. ClientSession별 send_mutex 적용
7. Broadcast MVP 구현
8. clients 컨테이너 최종 결정 및 교체
9. condition_variable 공부
10. 자료구조 몇 개 구현해보기
11. lock-free stack 가볍게 구현해보기
12. send queue / worker thread 구조 검토