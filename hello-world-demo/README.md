# ARXML Configuration Studio — Study Branch

## 학습 및 개발 진입점 (study)

이 브랜치는 `demo/layer-hello-world` → `demo/tdd-architecture-v03` → `study`의 순차적인 커밋 기록을 하나로 포함합니다. 브랜치 사이에 독립적인 변경 충돌이 없어 가장 최신 브랜치에서 `study`를 만들었습니다. `main`은 변경하지 않았습니다.

이번 추가 리팩토링은 사용자가 공유한 아래 두 자료의 *변경하기 쉬운 코드* 원칙을 직접 적용합니다.

- [토스: 선언적인 코드 작성하기](https://toss.tech/article/frontend-declarative-code): 작업 의도를 드러내는 추상화, 재사용 여부와 요구 변경에 따른 추상화 수준 선택
- [Frontend Fundamentals: 좋은 코드를 위한 4가지 기준](https://frontend-fundamentals.com/code-quality/code/): 가독성·예측 가능성·응집도·결합도

### v0.5 변경 내용

- `react/studio-model.ts`: 장면 목록, ECU UI 메타데이터, 이전/다음 전환, 선택 상태 조회를 하나의 UI 기능 모듈로 분리
- `react/App.tsx`: 모델의 선언적인 데이터와 함수를 사용하여 카드 및 ECU UI 표시
- 이전/다음 장면 이동 버튼과 순환 규칙 추가 (Studio → Topology → Detail → Night → Live 3D → Studio)
- `tests/studio-model.test.ts`: 장면 이동, 순환, 이미지 경로, ECU 상태 파생 계산의 독립 단위 테스트
- `tests/scenes.test.cjs`: 실제 에셋 파일과 장면 목록을 검증하도록 수정
- `npm run test:all`: Backend, Electron preload, 에셋, Three.js, UI 상태 테스트, 프론트 빌드를 순서대로 실행

기능별로 하나의 모듈을 두되 보안상의 preload, CMake 등 도구별 필수 파일은 유지했습니다. UI/DB/실제 Three.js를 한 파일에 억지로 넣지 않습니다.

### 시작하기

```bash
git clone -b study https://github.com/DongHeeKw/c_arxml_engine.git
cd c_arxml_engine/hello-world-demo

# C++ 빌드: Linux
cmake -S engine -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# UI 없는 C++ ↔ SQLite 테스트와 Electron IPC 계약
npm test

# UI 상태/장면 전환의 독립 테스트
npm install
npm run test:ui
npm run test:three

# 실제 앱
npm run build
npm start
```

기존 v0.4 문서의 `study` URL은 이 브랜치에서는 `study`로 바꾸어 사용하면 됩니다.

**검증 상태**: GitHub 쓰기 및 파일 재조회 확인. 현재 도구 실행 환경에서는 GitHub로부터 전체 저장소를 clone할 네트워크 접근이 없어 C++ 컴파일, Node 테스트, Electron 실제 렌더링을 끝까지 검증하지 못했습니다. 따라서 테스트 **추가 완료**와 테스트 **통과 검증**은 다릅니다.

---

## v0.4 — Visual Studio / Scene Gallery

v0.3 TDD 아키텍처는 유지하고, 참고 사진의 분위기에 맞춰 작업 화면을 개선했습니다.

### 새 화면 구성

| 화면 | 에셋 파일 | 사용 목적 |
|---|---|---|
| Studio | [studio-day.svg](public/assets/studio-day.svg) | 차량 투명 바디, ECU 오버레이를 보여주는 기본 장면 |
| Topology | [topology-map.svg](public/assets/topology-map.svg) | 차량 상단에서 네트워크와 ECU를 비교 |
| ECU Detail | [module-detail.svg](public/assets/module-detail.svg) | ECU 검사 시점을 표현 |
| Night | [inspection-night.svg](public/assets/inspection-night.svg) | 어두운 환경과 발광 연결선 비교 |
| Live 3D | [Vehicle.tsx](three/Vehicle.tsx) | 실제 Three.js WebGL로 ECU를 선택하는 인터랙티브 장면 |

### 작동 범위와 한계

- Studio/Topology/ECU Detail/Night는 **SVG 참고 일러스트**이며 실시간 3D나 실제 ECU 좌표 데이터가 아닙니다.
- 상단 **Live 3D** 버튼에서 Three.js 렌더러로 전환할 수 있습니다. 이미지 갤러리에서 표시되는 ECU는 정적 일러스트입니다.
- 왼쪽 Project Explorer 또는 화면 아래 ECU 버튼을 선택하면 오른쪽 Properties가 변경됩니다.
- Properties의 Enabled 체크박스만 실제 C++ 및 SQLite 상태를 변경합니다.
- DB History 버튼에서 실제 변경 이력을 확인할 수 있습니다.
- 장면과 선택 상태는 현재 UI 메모리 상태이며 다시 실행하면 기본값으로 돌아갑니다.
- DB 상태는 영구 저장됩니다. Import DBC와 Generate Code는 아직 데모용 비활성 버튼입니다.
- 원본 고해상도 생성 이미지는 SVG와 별개입니다. GitHub에 등록된 그림은 위에 링크된 SVG 4개이며, 원본 생성 이미지가 GitHub에 업로드됐다는 뜻이 아닙니다.

### 학습용 핵심 코드

- [App.tsx](react/App.tsx): scenes[] 데이터만 추가하여 장면 카드를 늘리는 **선언적 UI** 구성
- [Vehicle.tsx](three/Vehicle.tsx): 실시간 렌더링과 상태에 따른 ECU 색상 변경
- [scenes.test.cjs](tests/scenes.test.cjs): 에셋 4개와 UI 참조가 유지되는지 검사
- [public/assets](public/assets): Vite가 정적 파일을 dist/assets로 복사하는 경로

```bash
npm test                 # C++/DB + IPC + 에셋 계약 테스트
npm install
npm run test:three       # Three.js 계산 로직 테스트
npm run build            # TypeScript/Vite 빌드
npm start                # Electron UI 실행
```

참고: 이미 빌드된 dist/가 있다면 코드를 바꾼 뒤 `npm run build`를 다시 실행해야 변경한 장면이 보입니다.

### 상태

GitHub 파일 커밋은 확인됐지만 해당 환경의 C++ 컴파일, TypeScript 빌드, 실제 Electron GUI 및 GPU 렌더링 결과는 아직 검증되지 않았습니다.

---

## v0.3: 선언적 설계 + 테스트 우선 개발

### 참고한 글과 적용 방법

| 읽을거리 | 핵심 개념 | 이번 데모에서 확인할 위치 |
|---|---|---|
| [토스: 선언적인 코드 작성하기](https://toss.tech/article/frontend-declarative-code) | 세부 실행 과정보다는 의도를 표현하는 추상화 | `three/Vehicle.tsx`의 `ecuVisualState`, `pointerToNdc` |
| [Frontend Fundamentals: 좋은 코드를 위한 4가지 기준](https://frontend-fundamentals.com/code-quality/code/) | 가독성, 예측 가능성, 응집도, 결합도 | Electron IPC 계약, Three.js 순수 함수, tests 폴더 |
| [SQLite CREATE TABLE](https://www.sqlite.org/lang_createtable.html) | PRIMARY KEY, NOT NULL, CHECK, FK로 불변식 보호 | `database/schema.sql` |
| [SQLite Transactions](https://www.sqlite.org/lang_transaction.html) | BEGIN IMMEDIATE, COMMIT, ROLLBACK | `engine/main.cpp` |
| [SQLite Foreign Keys](https://www.sqlite.org/foreignkeys.html) | 관계의 무결성과 PRAGMA foreign_keys | `database/schema.sql` |
| [Martin Fowler: Gateway](https://martinfowler.com/articles/gateway-pattern.html) | 외부 시스템/DB 통신을 추상화 경계 뒤에 배치 | `engine/main.cpp`의 SQL 접근 함수 |
| [Martin Fowler: Repository](https://martinfowler.com/eaaCatalog/repository.html) | 복잡한 도메인과 저장소를 분리할 때 고려 | 이번에는 의도적으로 미도입 |
| [Martin Fowler: Evolutionary Database Design](https://martinfowler.com/articles/evodb.html) | 스키마 변경과 테스트를 함께 버전 관리 | schema.sql 및 tests; 마이그레이션은 향후 과제 |

이 참고자료의 원칙을 이 데모에 적용한 방식이지, 해당 글들이 이 코드 구조를 직접 권장한다는 의미는 아닙니다.

### DB 경계 원칙

- React와 Three.js는 **SQL을 호출하지 않습니다**. 데이터 요청은 오직 Electron preload → main IPC → C++ → SQLite를 거칩니다.
- SQLite는 PK/NOT NULL/CHECK/FK 등 가능한 불변식을 보장합니다.
- C++에서 SQL은 문자열 연결 대신 바인딩 파라미터로 실행합니다.
- **한 상태 변경과 그 변경 이력 저장은 하나의 트랜잭션**으로 취급합니다.
- 같은 상태를 다시 저장해도 변경 이력이 중복으로 남지 않습니다.
- 테스트는 OS 임시 디렉터리에 독립적인 SQLite DB를 생성하고 종료 후 삭제합니다.

### 실습: 영속성, 이력, 무결성

```bash
./build/ecu_engine demo-v03.sqlite list
./build/ecu_engine demo-v03.sqlite set VCU 1
./build/ecu_engine demo-v03.sqlite set VCU 1
./build/ecu_engine demo-v03.sqlite history
./build/ecu_engine demo-v03.sqlite check
npm test
npm run test:three
npm run test:all
```

위 명령에서 VCU의 두 번째 설정은 중복 이력을 추가하지 않아야 합니다. `history`에는 최초 변경만 기록됩니다.
GUI에서는 `DB History` 버튼으로 동일한 SQLite 변경 이력 데이터를 IPC를 통해 확인할 수 있습니다.

### TDD 단계별 목표

| 테스트 | 실패시 의심할 경계 | 검증하려는 계약 |
|---|---|---|
| `tests/smoke.cjs` | Node → C++ → SQLite | 초기화, 재조회, 변경, 이력, 유효성, 무결성 |
| `tests/electron.test.cjs` | preload → IPC | UI에 노출된 함수와 채널 이름 |
| `tests/three.test.ts` | React state → Three 계산 | 선택 색상 우선순위 및 좌표 정규화 |

실제 Electron 창 렌더링 및 WebGL GPU 출력까지 자동 검증하는 E2E 테스트는 아직 없습니다.
Red → Green → Refactor 과정의 모든 중간 커밋을 보존한 것은 아닙니다. 테스트와 리팩토링 결과가 버전 관리에 반영되었습니다.

### 기존 DB와의 호환성 참고

기존 v0.2 DB의 `ecu` 테이블은 유지되며, `ecu_change` 이력 테이블이 추가됩니다.
이전 버전에서 이미 일어난 상태 변경은 이력에 소급해 기록되지 않습니다.
현재는 학습용 스키마 초기화 방식입니다. Production에서는 명시적 버전 마이그레이션, 백업, 복구, 동시성 부하 테스트가 필요합니다.

---

## 빠른 실행 가이드

준비: Git, Node.js 22.12 이상, npm, CMake 3.16 이상, C++17 컴파일러, SQLite3 개발 라이브러리. OS별 상세 준비 및 Windows 명령은 아래 상세 가이드를 참고하세요.

### STEP 1. 소스코드 가져오기

```bash
git clone -b study https://github.com/DongHeeKw/c_arxml_engine.git
cd c_arxml_engine/hello-world-demo
```

### STEP 2. C++ 및 DB 실행 (Linux/macOS)

```bash
cmake -S engine -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/ecu_engine demo.sqlite list
```

Windows Visual Studio 멀티 구성 빌드는 아래 상세 가이드의 Windows 명령과 ECU_ENGINE 환경변수를 사용하세요.

### STEP 3. UI 없이 통합 테스트

```bash
npm test
```

Electron이나 React 실행, npm install 없이 Node.js → C++ → SQLite 연동을 검사합니다.

### STEP 4. React 및 Three.js 빌드

```bash
npm install
npm run test:three
npm run build
```

### STEP 5. Electron 실행

```bash
npm start
```

## 공부하기 좋은 소스코드 순서

| 순서 | 파일 | 공부할 내용 |
|---|---|---|
| 1 | [schema.sql](database/schema.sql) | DB 테이블 생성 |
| 2 | [main.cpp](engine/main.cpp) | SQL 실행, JSON 반환 |
| 3 | [smoke.cjs](tests/smoke.cjs) | JavaScript에서 C++ 호출 |
| 4 | [main.cjs](electron/main.cjs) | Electron 프로세스 |
| 5 | [preload.cjs](electron/preload.cjs) | IPC 인터페이스 |
| 6 | [App.tsx](react/App.tsx) | UI, Component, State |
| 7 | [Vehicle.tsx](three/Vehicle.tsx) | 3D 객체 및 렌더링 |
| 8 | [smoke.cjs](tests/smoke.cjs) | DB 트랜잭션/영속성/멱등성 회귀 테스트 |
| 9 | [electron.test.cjs](tests/electron.test.cjs) | preload의 제한된 IPC 계약 테스트 |
| 10 | [three.test.ts](tests/three.test.ts) | Three.js 스타일 및 화면 좌표 단위 테스트 |

## 검증 상태

| 항목 | 상태 |
|---|---|
| GitHub 브랜치 생성 및 기존 데모 파일 13개 커밋 | 완료 |
| 기존 main 브랜치 변경 | 없음 |
| 현재 브랜치 C++ 빌드 및 Node.js 통합 테스트 | 미검증 |
| TypeScript / React / Three.js 빌드 | 미검증 |
| 실제 Electron GUI 실행 | 미검증 |

구현과 커밋 완료는 실제 실행 검증 완료를 의미하지 않습니다.

---

## 상세 가이드

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
