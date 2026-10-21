# rtprobe — Winlator 레이트레이싱 테스트

[![build-windows](https://github.com/shin5968/TestRT/actions/workflows/build.yml/badge.svg)](https://github.com/shin5968/TestRT/actions/workflows/build.yml)

Winlator(Android)에서 **D3D12 + VKD3D-Proton + Turnip** 경로가 레이트레이싱을 어디까지
지원하는지 진단하고, 같은 경로로 소프트웨어 레이트레이서를 렌더링해 성능을 측정하는
단일 Windows x86_64 실행 파일입니다.

## 왜 이렇게 만들었나

현재 Winlator에서 D3D12는 `vkd3d-proton`이 Vulkan으로 번역합니다. 결과는 컨테이너가 쓰는
Vulkan 드라이버에 따라 크게 달라집니다.

- **Adreno + Turnip**: Turnip 25.0+에서 `VK_KHR_ray_query`만 지원하고
  `VK_KHR_ray_tracing_pipeline` / `VK_KHR_acceleration_structure`는 미지원 →
  vkd3d-proton DXR이 요구하는 확장이 없어 `RaytracingTier = 0`이 될 가능성이 높음
- **Xclipse 940 (Exynos 2400, RDNA3) + RDVK 계열 커스텀 드라이버**: RDNA3의 RT 유닛과
  드라이버 구현에 따라 `VK_KHR_ray_tracing_pipeline`/AS 확장이 노출될 수 있음 →
  `RaytracingTier 1.0/1.1`이 나올 수 있으므로 **반드시 probe 결과로 확인**해야 함
- vkd3d-proton의 DXR: `VK_KHR_ray_tracing_pipeline` + `VK_KHR_acceleration_structure`
  (+ `deferred_host_operations`)를 **필수**로 요구

이 프로그램은 어떤 드라이버든 확장 노출 여부를 그대로 리포트하고, 공통적으로 동작하는
**D3D12 컴퓨트 셰이더 소프트웨어 패스 트레이서**로 실제 GPU 성능을 측정합니다.
(소프트웨어 패스 트레이싱도 진짜 레이 트레이싱이며, 하드웨어 RT 유닛을 쓰지 않을 뿐입니다.)

## 기능

- **진단**: DXGI 어댑터, feature level, `RaytracingTier`, 셰이더 모델, `ID3D12Device5`/`GraphicsCommandList4`
  지원, vkd3d-proton/와인/드라이버 버전, `VKD3D_*` 환경변수, underlying Vulkan의 RT 확장 노출 여부
- **데모**: Cornell box + 구체 3개, 면광, CPU BVH, 컴퓨트 패스 트레이서, 점진적 누적(progressive)
- **하드웨어 RT 데모**: Vulkan `VK_KHR_acceleration_structure` + `VK_KHR_ray_query`로
  같은 장면을 실제 RT 경로로 렌더링 (`4` 키 또는 `--vulkan-rt`). 결과는 CPU로 읽어
  D3D12 오버레이 컴포지터로 표시합니다. DXIL/DXR RTPSO 없이 RT 하드웨어를 직접 검증합니다.
- **벤치마크**: 고정 워크로드, GPU 타임스탬프(가능 시) + CPU 프레임 타임, p50/p95/p99, rays/s
- **리포트**: JSON + 로그 + BMP 스크린샷

## 빌드 (Termux)

```bash
pkg install llvm-mingw-w64 cmake ninja
./build.sh
```

결과물: `build/rtprobe.exe` (PE32+ x86-64 GUI).

툴체인/헤더 참고:
- Vulkan 헤더는 `third_party/vulkan`에 vendoring 되어 있습니다.
- Windows UCRT DLL(`api-ms-win-crt-*`, `ucrtbase.dll`)과 `VERSION.dll`을 사용합니다.
  Wine/Winlator에는 기본 포함되어 있습니다.
- HLSL은 **런타임에 `d3dcompiler_47.dll`(DXBC, SM 5.0)로 컴파일**합니다. DXIL/DXC는 사용하지 않습니다.

HLSL 문법만 빠르게 검사하려면:

```bash
python3 -I tools/check_shaders.py
```

Vulkan RT 셰이더(`src/vk_rt.comp`)는 SPIR-V로 미리 컴파일해 `src/vk_rt_spv.h`에
임베드되어 있습니다. 셰이더를 수정하면 다음으로 재생성하세요:

```bash
glslangValidator -V --target-env vulkan1.2 -S comp src/vk_rt.comp -o build/vk_rt.comp.spv
python3 -I tools/gen_spv_header.py build/vk_rt.comp.spv src/vk_rt_spv.h vk_rt_comp_spv
```

## Winlator에서 실행

1. `build/rtprobe.exe`를 폰의 다운로드 폴더 등으로 복사합니다.
2. Winlator 컨테이너 설정:
   - Winlator 11.1 이상 권장
   - **DX wrapper: VKD3D (2.12 또는 2.14.1)**
   - **Turnip 25.0.0 이상** (Adreno 740+)
   - Box64, 해상도 1280x720 권장
3. 컨테이너에서 exe를 실행합니다. 파일 탐색기에서 더블클릭하거나,
   Winlator의 "Run"으로 실행하면 됩니다.

### 진단만 실행 (창 없음, 가장 빠름)

```text
rtprobe.exe --probe-only
```

실행 후 exe 옆에 `rtprobe-report.json`과 `rtprobe.log`가 생성됩니다.
JSON에서 다음을 확인하세요.

```json
"raytracing_tier": 0,
"raytracing_tier_text": "0 (NOT_SUPPORTED)",
"vulkan": {
  "ray_query": true,
  "ray_tracing_pipeline": false,
  "acceleration_structure": false,
  "driver_name": "...",
  "driver_info": "..."
}
```

### 무인 벤치마크

```text
rtprobe.exe --bench
rtprobe.exe --bench --seconds 20
rtprobe.exe --bench --frames 300 --width 960 --height 540
```

`--bench`는 창을 띄운 채 자동으로 워밍업 후 측정하고, 끝나면 종료합니다.
기본 측정: 1280x720, 1 spp/frame, 4 bounces, 워밍업 10초, 600프레임.

### DXR RTPSO 최소 테스트

```text
rtprobe.exe --dxr-test
```

D3D12로 1개 삼각형 BLAS/TLAS를 만들고, 임베드된 DXIL(`lib_6_3`)로 RTPSO를 생성한 뒤
64x64 레이를 `DispatchRays`로 추적해 히트 수를 셉니다. 소프트웨어/HW RT 데모와 달리
**Windows 게임과 동일한 D3D12 DXR 경로**를 검증합니다.

결과는 리포트의 `dxr_test`에 기록됩니다:

```json
"dxr_test": {
  "attempted": true,
  "as_built": true,
  "state_object_created": true,
  "dispatched": true,
  "width": 64, "height": 64, "total": 4096,
  "hits": 4096,
  "stage": "done",
  "error": ""
}
```

`stage`는 실패 지점(`geometry`, `blas`, `tlas`, `root_signature`, `state_object`,
`shader_table`, `output`, `commands`, `readback`)을 알려줍니다.

### 하드웨어 RT 벤치마크

```text
rtprobe.exe --bench-hw
rtprobe.exe --bench-hw --seconds 20
rtprobe.exe --bench-hw --frames 300 --width 960 --height 540
```

Vulkan ray query로 같은 장면을 프레임마다 1샘플씩(누적 없이) 렌더링하고,
Vulkan 타임스탬프로 **GPU 프레임 타임**을, CPU 시간으로 **readback 포함 프레임 타임**을
측정합니다. 결과는 리포트의 `hardware_rt_benchmark` 섹션에 기록됩니다.
소프트웨어 벤치마크(`--bench`)와 같은 해상도/바운스에서 비교할 수 있습니다.

### 대화형 모드

인자 없이 실행하면 데모가 바로 시작됩니다. 터치/마우스로 카메라를 돌릴 수 있습니다.

| 키 | 동작 |
|---|---|
| `4` | **HW RT(Vulkan ray query) 데모 토글** |
| `1` / `2` / `3` | 바운스 2 / 4 / 6 |
| `R` | 누적 리셋 |
| `B` | 벤치마크 시작 (대화형: 워밍업 3초) |
| `F1` | 오버레이 토글 |
| `F2` 또는 `S` | 스크린샷 (`rtprobe-screenshot.bmp`) |
| `V` | VSync 토글 |
| `Esc` | 종료 |

HW RT 데모는 창과 같은 해상도(기본 1280x720)로 렌더링하고, 결과를 CPU로 읽어
D3D12 오버레이 화면에 그대로 표시합니다. CPU readback 비용이 FPS에 포함됩니다.
초기화에 실패하면 오버레이와 `rtprobe.log`에 `Vulkan RT: <이유>`가 표시되고
소프트웨어 데모로 돌아갑니다.

## CLI 옵션

```text
--probe-only            진단만 수행하고 종료 (창 없음)
--vulkan-rt, --hwrt     시작 시 Vulkan ray query HW RT 데모 활성화
--bench                 워밍업 → 측정 → 리포트 → 종료 (소프트웨어 RT)
--bench-hw, --hwrt-bench  Vulkan ray query 하드웨어 RT 벤치마크 → 리포트 → 종료
--dxr-test              D3D12 DXR RTPSO 최소 테스트 → 리포트 → 종료
--seconds N             고정 시간 측정 (N초)
--frames N              고정 프레임 측정 (기본 600, 최대 2000)
--width W --height H    렌더/창 해상도 (기본 1280x720)
--vsync                 VSync 켜기 (기본 OFF)
--borderless            테두리 없는 창
--debug                 예약됨 (디버그 레이어는 기본 비활성)
```

## 출력 파일

`rtprobe.exe`가 있는 폴더에 생성됩니다.

- `rtprobe-report.json` — 진단 + 벤치마크 결과 (기기/드라이버 비교용)
- `rtprobe.log` — 전체 로그 (DLL 버전, HRESULT, 셰이더 컴파일 에러 등)
- `rtprobe-screenshot.bmp` — 마지막 프레임 스크린샷

## 리포트 읽는 법

핵심 필드:

- `probe.raytracing_tier`: 0이면 DXR API 자체가 비활성. 10(1.0)/11(1.1)이면 API는 노출.
- `probe.vulkan.ray_tracing_pipeline`: vkd3d-proton DXR에 필요한 파이프라인 확장.
  `false`면 tier가 0인 이유가 명확합니다.
- `probe.vulkan.acceleration_structure`: 가속 구조 확장. `false`면 BLAS/TLAS 자체가 불가.
- `probe.vulkan.ray_query`: Turnip의 인라인 레이 쿼리. `true`여도 DXR 게임은 동작하지 않습니다.
- `probe.modules.d3d12core.version`: vkd3d-proton 빌드 버전.
- `benchmark.gpu_frame`: GPU 타임스탬프 기반 프레임 타임 (가능한 경우).
  `gpu_timing=false`면 CPU present-to-present 폴백입니다.
- `benchmark.primary_rays_per_sec`: 해상도 × spp × 프레임 / 시간 (정확값).
- `benchmark.total_rays_per_sec`: 셰이더 내부 32비트 카운터 기반 (장시간/고해상도에서 wrap 가능).
- `hardware_rt_benchmark`: Vulkan ray query 벤치 결과. `gpu_frame`은 Vulkan 타임스탬프,
  `cpu_frame`은 CPU readback을 포함한 프레임 시간입니다. `timestamp_period_ns`로 원시 틱을
  변환하며, `gpu_timing=false`면 CPU 값만 신뢰하세요.
- `hardware_rt_benchmark.device`/`driver`/`extensions`: 실제 사용된 Vulkan RT 드라이버 정보.

공정 비교 규칙: 동일 `width/height/spp/bounces/scene.hash/shader_hash`에서만 비교하고,
여러 번 실행해 중앙값을 사용하세요. 발열 스로틀링 때문에 첫 실행은 버리는 것을 권장합니다.

## 알려진 한계 / 문제 해결

- **D3D12 DXR RTPSO는 최소 테스트만 제공합니다.** DXIL은 GitHub Actions에서 DXC로 생성해
  임베드했고, `--dxr-test`가 BLAS/TLAS → RTPSO → DispatchRays → 히트 수까지 검증합니다.
  Cornell box를 DXR로 렌더링하는 시각 모드는 아직 구현하지 않았습니다.
- `--dxr-test`가 `state_object` 단계에서 실패하면 vkd3d-proton/드라이버의 DXR 셰이더 모델
  지원 문제일 수 있습니다. `VKD3D_SHADER_MODEL=6_6`을 컨테이너 환경변수로 설정해 재시도하세요.
- **HW RT는 Vulkan 경로로 검증합니다.** `VK_KHR_acceleration_structure` +
  `VK_KHR_ray_query`(+ buffer device address)가 필요합니다. vkd3d-proton/DXR과 같은
  디바이스 드라이버를 사용하므로, 이 모드가 동작하면 하드웨어 RT 자체는 정상입니다.
- HW RT 결과는 매 프레임 CPU로 읽어 D3D12 화면에 올리므로, 인터랙티브 모드의 FPS에는
  readback 비용이 포함됩니다. GPU 처리량은 `--bench-hw`의 `gpu_frame` 지표를 사용하세요.
- 소프트웨어/HW RT 장면은 구체 표현이 다릅니다(소프트웨어는 해석적 구체, HW는 삼각형 메시).
  성능 비교 시 이 차이를 감안하세요.
- **D3DCompile DXBC가 거부될 수 있습니다.** `rtprobe.log`의 `D3DCompile ... failed`와
  컴파일러 메시지를 확인하세요. vkd3d-proton 빌드에 따라 DXBC 지원이 다를 수 있습니다.
- **검은 화면**: `--borderless`, 해상도 변경, `VKD3D_CONFIG` 제거 후 재시도.
  `rtprobe.log`에서 `Present failed`, `CreateSwapChainForHwnd failed`를 확인하세요.
- **GPU 타임아웃/멈춤**: 해상도를 낮추고(예: 960x540), 1 spp/frame을 유지하세요.
  로그에 `GPU fence timeout`이 남습니다.
- **리포트가 없음**: exe 폴더가 쓰기 불가면 `%TEMP%`를 확인하세요.
  `d3d12.dll` 로드 실패 시 컨테이너 DX wrapper를 VKD3D로 설정해야 합니다.
- **Turnip 미사용(벤더 드라이버)**: 진단 결과가 달라질 수 있습니다. 비교용으로 유용합니다.

## 파일 구조

```text
CMakeLists.txt, cmake/mingw-llvm.cmake, build.sh
src/main.cpp            WinMain + CLI
src/app.cpp/.h          창, 메시지 루프, 모드/입력, 벤치 드라이버
src/d3d12_ctx.cpp/.h    디바이스/큐/스왑체인/펜스, 동적 D3D12 로딩
src/probe.cpp/.h        DXGI/D3D12/모듈/Vulkan 진단
src/scene.cpp/.h        Cornell box + BVH
src/renderer.cpp/.h     PSO/루트 시그니처/디스패치, 타임스탬프
src/shaders.h           HLSL (패스 트레이서 + 컴포지트)
src/vk_rt.cpp/.h        Vulkan 가속구조 + ray query HW RT
src/vk_rt.comp          HW RT GLSL 셰이더 (SPIR-V로 임베드)
src/vk_rt_spv.h         임베드된 SPIR-V (생성 파일)
src/dxr_test.cpp/.h     D3D12 DXR RTPSO 최소 테스트
src/dxr_test.hlsl       DXR 셰이더 (DXIL로 임베드)
src/dxr_test_dxil.h     임베드된 DXIL (생성 파일)
src/overlay.cpp/.h      GDI 기반 텍스트/이미지 오버레이
src/bench.cpp/.h        통계/백분위
src/report.cpp/.h       JSON 리포트
src/util.cpp/.h         로깅/경로/버전/COM 도우미
third_party/vulkan      Vulkan 헤더 (vendoring)
tools/check_shaders.py  HLSL 문법 검사 (개발용)
tools/gen_spv_header.py SPIR-V → C++ 헤더 변환기
```
# TestRT
