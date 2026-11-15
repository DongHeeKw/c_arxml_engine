# ARXML Configuration Studio — Layer Hello World v0.2

AUTOSAR 실제 설정 엔진이 아닌 독립적인 학습용 데모입니다. 기존 프로젝트를 변경하지 않습니다.
사진을 참고한 어두운 3단 UI, 4개 ECU, 3D 차량, 속성 패널, 콘솔과 상태 표시줄을 제공합니다.
차량은 외부 모델 없이 기본 Three.js 도형으로 구성됩니다. Import DBC / Generate Code는 비활성 예시입니다.

## 1. 파일별 역할

| 파일 | 역할 |
|---|---|
| electron/main.cjs | Electron 윈도우 실행, IPC 수신, C++ 프로세스 실행 |
| electron/preload.cjs | 격리된 UI에 제한된 API 노출 |
| react/App.tsx | Toolbar, Explorer, Properties, Console, State, CSS |
| three/Vehicle.tsx | Scene, Camera, Renderer, Vehicle, ECU Mesh, Raycaster |
| engine/main.cpp | 명령행 인자 처리, SQLite 조회/수정, JSON 반환 |
| engine/CMakeLists.txt | C++ 빌드와 SQLite 링크, schema.sql 임베딩 |
| database/schema.sql | DB 테이블과 초기값 |
| tests/smoke.cjs | GUI 없는 Node → C++ → SQLite 통합 테스트 |

기능별 폴더 하나, 주요 소스 하나를 원칙으로 합니다. preload와 CMake 등 실행 및 보안 설정은 예외입니다.
MCU/Network/BSW는 UI 샘플 데이터입니다. Enabled만 실제 DB에 저장됩니다.

## 2. 준비

Node.js 22.12 이상, npm, CMake 3.16 이상, C++17 컴파일러, SQLite3 개발 헤더 및 라이브러리가 필요합니다.
Ubuntu 예시: sudo apt install build-essential cmake libsqlite3-dev
Windows는 CMake가 SQLite3를 찾도록 승인된 개발환경의 패키지 또는 toolchain을 설정해야 합니다.
회사에서 GUI 혹은 설치가 금지된 경우에는 허용된 환경에서 아래의 CLI만 사용하세요.

모든 명령은 hello-world-demo 폴더에서 실행합니다.

## 3. Layer 1: C++만 실행 (Linux/macOS 또는 단일 구성 CMake)

```sh
cmake -S engine -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/ecu_engine demo.sqlite list
./build/ecu_engine demo.sqlite set VCU 1
./build/ecu_engine demo.sqlite list
```

첫 list에서 ADAS, BCM, CGW는 true, VCU는 false입니다. set 이후 VCU가 true로 유지됩니다.
stdout은 JSON 응답, stderr는 디버그 로그입니다. DB는 첫 실행 시 생성됩니다.

Windows Visual Studio 멀티 구성 빌드:
```powershell
cmake -S engine -B build
cmake --build build --config Debug
$env:ECU_ENGINE = (Resolve-Path build/Debug/ecu_engine.exe).Path
& $env:ECU_ENGINE demo.sqlite list
```

SQLite CLI가 설치되어 있다면 sqlite3 demo.sqlite "SELECT * FROM ecu;"로 DB를 직접 확인할 수 있습니다.

## 4. Layer 2: Node → C++ → DB 통합 테스트

```sh
npm test
```

이 테스트 자체는 npm install이나 GUI가 필요 없습니다.
임시 DB에 대해 초기값, 변경, 별도 프로세스 재조회(영속성)를 검사합니다.
Windows에서는 위 ECU_ENGINE 환경변수를 유지하세요.

## 5. Layer 3: React + Three.js 빌드

```sh
npm install
npm run build
```

TypeScript 타입 검사와 Vite 번들링을 수행해 dist/index.html 및 JS 에셋을 생성합니다.
이 단계에서 Electron과 C++ 엔진을 실행하지 않습니다.

## 6. Layer 4: Electron GUI 실행

```sh
npm start
```

Electron main process가 윈도우를 생성하고 dist/index.html을 로드합니다.
preload가 window.ecu.list/set을 노출하고 React가 목록을 요청합니다.
C++ 엔진은 IPC 요청마다 별도 프로세스로 실행되며 요청 종료 후 종료됩니다.
SQLite 파일은 Electron app.getPath('userData') 아래 ecu.sqlite에 생성됩니다.
CLI demo.sqlite와 GUI ecu.sqlite는 서로 다른 DB입니다.

## 7. 디버깅 순서

1. React App.tsx: change()에 breakpoint. name과 enabled 확인.
2. preload.cjs: ipcRenderer.invoke 호출 확인.
3. main.cjs: ipcMain.handle 및 engine()에서 인자와 stdout 확인.
4. main.cpp: main(), sqlite3_prepare_v2(), sqlite3_step()까지 GDB/Visual Studio로 확인.
5. DB: SELECT로 저장값 확인.
6. React: setEcus(rows)에서 응답 확인.
7. Three.js Vehicle.tsx: 마지막 useEffect에서 색상 변경 확인.

GUI DevTools는 Linux/macOS에서 DEMO_DEBUG=1 npm start, PowerShell에서
$env:DEMO_DEBUG='1'; npm start 로 열 수 있습니다.
Electron main 로그와 C++ stderr는 실행 터미널, React 및 Three.js 로그는 DevTools에 표시됩니다.
콘솔 패널은 React에서 실제 요청/응답을 표시하며 SQLite 내부 로그를 흉내 내지 않습니다.

## 8. 제한 사항

실제 AUTOSAR 규칙, DBC Import, 코드 생성은 구현하지 않습니다.
Next.js, DB 서버, Docker를 사용하지 않습니다.
이 브랜치에 커밋된 코드의 실제 GUI 실행과 전체 빌드 결과는 사용 환경에서 확인해야 합니다.
