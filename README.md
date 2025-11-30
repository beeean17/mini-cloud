# Mini Cloud

Concurrent CLI 기반 파일 전송 시스템을 위한 학습용 레포입니다. 현재 단계에서는 프로토콜 헤더와 직렬화/역직렬화 유틸리티를 정의했습니다.

## Layout

- `include/mc_protocol.h` : 명령 타입, 헤더 구조체, 직렬화 함수 시그니처 정의
- `include/mc_server.h` : 서버 설정 구조체 및 실행 진입점 선언
- `src/common/mc_protocol.c` : 헤더 빌더, validation, 네트워크 바이트 오더 변환, read/write 보조 함수 구현
- `src/server/` : `mc_server.c`(리스너/시그널/포크 워커 + 파일 업/다운로드/리스트 처리) + `main.c`(CLI 진입점)
- `tests/protocol_demo.c` : 파이프를 이용해 헤더 송수신을 검증하는 간단한 샘플
- `tests/smoke_client.c` : 서버에 접속해 QUIT 명령을 전송하는 미니 클라이언트
- `src/client/`, `include/mc_client.h` : CLI 기반 클라이언트, 명령 파서·업로드/다운로드/LIST 직렬화 및 응답 처리
- `tests/multi_client.sh` : 다수의 클라이언트를 동시에 구동해 업/다운로드 스트레스 테스트 수행
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

### 클라이언트 CLI

```bash
make test-client
```

위 타깃은 서버를 백그라운드로 실행한 뒤 실제 CLI(`bin/client`)에 `LIST → UPLOAD → DOWNLOAD → QUIT` 시퀀스를 주입해 저장소 입·출력을 검증합니다. 수동으로 사용하려면 서버를 별도로 띄운 다음 `./bin/client 127.0.0.1 <port>`로 접속하고 프롬프트에서 명령을 입력하면 됩니다.

### 동시성 스트레스 테스트

```bash
make test-stress
```

`tests/multi_client.sh`가 여러 클라이언트를 병렬로 띄워 반복적으로 `UPLOAD → DOWNLOAD → LIST → QUIT` 시퀀스를 수행합니다. 환경 변수 `CLIENTS`, `ROUNDS`, `PORT`로 부하 수준을 조절할 수 있으며, 실패 시 서버/클라이언트 로그가 출력됩니다.

## 보안·설정 관리

- **공유 토큰 인증**: 서버 실행 전에 `MC_SERVER_TOKEN` 또는 `MC_SERVER_TOKEN_FILE=/path/to/secret`를 지정하면 모든 클라이언트가 최초 접속 시 `AUTH` 명령을 통해 동일한 토큰을 제시해야 합니다. 클라이언트는 세 번째 인자로 직접 토큰을 넘기거나, `MC_CLIENT_TOKEN`/`MC_CLIENT_TOKEN_FILE` 환경 변수를 통해 자동으로 토큰을 전송합니다. 서버는 인증 성공 시 `MC_CMD_AUTH` 응답으로 `AUTH OK` 메시지를 돌려줍니다.
- **업로드 용량 제한**: `MC_MAX_UPLOAD_BYTES=10485760` 처럼 설정하면 지정된 바이트 이상인 파일 업로드는 거부되어 `[SERVER ERROR] Upload exceeds limit (...)` 응답을 반환합니다. 0 또는 미설정이면 제한을 두지 않습니다.
- **리스너 백로그/스토리지 경로**: `MC_SERVER_BACKLOG`로 `listen()` 큐 길이를, `MC_STORAGE_DIR`로 2번째 인자를 생략했을 때 사용될 기본 저장소 경로를 제어할 수 있습니다.
- **테스트와 토큰**: `make test-server`, `make test-client`, `make test-stress`는 내부적으로 고정 토큰을 설정해 인증 경로를 항상 exercising 합니다. 필요 시 `AUTH_TOKEN`, `MAX_UPLOAD_BYTES` 등의 환경 변수로 오버라이드하세요.

| 환경 변수 | 설명 |
| --- | --- |
| `MC_SERVER_TOKEN` / `MC_SERVER_TOKEN_FILE` | 서버 측 공유 시크릿 문자열 또는 파일 경로. 설정 시 인증 활성화. |
| `MC_CLIENT_TOKEN` / `MC_CLIENT_TOKEN_FILE` | 클라이언트 측 토큰 제공. 서버가 토큰을 요구하지 않아도 전송 시 안전하게 무시됩니다. |
| `MC_MAX_UPLOAD_BYTES` | 허용되는 업로드 최대 크기 (바이트). 기본 0 (제한 없음). |
| `MC_SERVER_BACKLOG` | `listen()` 백로그 크기 (기본 16, 1–1024 허용). |
| `MC_STORAGE_DIR` | 2번째 인자를 생략한 경우 사용할 기본 저장소 디렉터리. |

### 배포·패키징 가이드
- `scripts/package.sh [버전]`을 실행하면 `dist/mini-cloud-<버전>.tar.gz`가 생성됩니다. (버전 미지정 시 UTC 타임스탬프 사용)
- 아카이브에는 `bin/server`, `bin/client`, `deploy/README_DEPLOY.md`, `deploy/server.env.example`, `deploy/mini-cloud.service`, 빈 `storage/` 디렉터리가 포함됩니다.
- `deploy/README_DEPLOY.md`를 참고해 `/opt/mini-cloud`에 압축을 해제하고 `/etc/mini-cloud/server.env`, `/etc/systemd/system/mini-cloud.service`를 설정하면 Oracle Cloud 같은 리눅스 서버에 즉시 배포할 수 있습니다.
- 실제 배포 직전에는 `deploy/PRE_DEPLOY_CHECKLIST.md`를 따라 네트워크·보안·스토리지·롤백 항목을 모두 확인하세요.

### 파일 전송 동작
- **UPLOAD `<path ...>`**: 지정한 로컬 파일들을 서버 `storage/` 디렉터리에 순차 업로드합니다. 서버는 임시 파일로 수신 후 원자적으로 교체하고, 각 파일마다 성공 메시지를 반환합니다.
- **DOWNLOAD `<filename ...>`**: 서버 저장소의 하나 이상 파일을 요청해 현재 디렉터리에 동일한 이름으로 저장합니다. 존재하지 않는 파일은 `MC_CMD_ERROR` 응답으로 안내되며 다른 파일은 계속 진행됩니다.
- **DOWNLOAD ALL**: `LIST` 결과에 나타난 모든 안전한 파일을 자동으로 다운로드합니다.
- **DELETE `<filename ...>`**: 서버 저장소에서 지정한 파일을 제거합니다. 존재하지 않는 파일은 오류 메시지를 반환하지만 나머지 요청은 계속 처리됩니다.
- **LIST**: 서버 `storage/`의 파일 목록을 개행으로 구분해 돌려줍니다.
- **QUIT**: 서버에서 연결을 정리하고 클라이언트를 종료합니다.
- **AUTH**: 토큰이 설정된 경우 클라이언트가 접속 직후 자동으로 전송하며, 서버는 토큰 검증 후 `AUTH OK` 또는 오류를 반환합니다.

민감한 설정/인증 자료는 `private/` 서브모듈에서 관리하고, 공개 레포에는 단순 샘플만 두는 것을 추천합니다.