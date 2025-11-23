# Mini Cloud

Concurrent CLI 기반 파일 전송 시스템을 위한 학습용 레포입니다. 현재 단계에서는 프로토콜 헤더와 직렬화/역직렬화 유틸리티를 정의했습니다.

## Layout

- `include/mc_protocol.h` : 명령 타입, 헤더 구조체, 직렬화 함수 시그니처 정의
- `include/mc_server.h` : 서버 설정 구조체 및 실행 진입점 선언
- `src/common/mc_protocol.c` : 헤더 빌더, validation, 네트워크 바이트 오더 변환, read/write 보조 함수 구현
- `src/server/` : `mc_server.c`(리스너/시그널/포크 워커 구현) + `main.c`(CLI 진입점)
- `tests/protocol_demo.c` : 파이프를 이용해 헤더 송수신을 검증하는 간단한 샘플
- `tests/smoke_client.c` : 서버에 접속해 QUIT 명령을 전송하는 미니 클라이언트
- `Makefile` : 빌드/테스트 타겟 제공 (`make test-protocol`, `make test-server` 등)

## Quick Start

### 프로토콜 유닛 테스트

```bash
make clean
make test-protocol
```

`bin/protocol_demo` 실행 결과로 직렬화된 헤더가 표준 출력에 표시됩니다.

### 서버 스모크 테스트

```bash
make clean
make test-server
```

위 명령은 서버(`bin/server`)와 스모크 클라이언트(`bin/smoke_client`)를 빌드한 뒤, 로컬 포트 9400에서 서버를 띄우고 QUIT 명령을 전송해 포크 워커·시그널 처리 루프가 제대로 작동하는지 확인합니다. 서버를 직접 실행하려면 `./bin/server <port> [storage_dir]` 형태로 구동하면 됩니다.