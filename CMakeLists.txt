cmake_minimum_required(VERSION 3.20)
project(DragonTower LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Visual C++ 재배포 패키지 없이 실행되도록 런타임을 정적으로 링크한다.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(DRAGON_BUILD_TESTS "핵심 규칙 테스트를 빌드한다" ON)

# ---- 핵심 규칙(플랫폼 무관) ----
add_library(dragon_core STATIC
    src/core/Species.cpp
    src/core/Enemy.cpp
    src/core/Tower.cpp
    src/core/DragonData.cpp
    src/core/Progression.cpp
    src/core/SaveData.cpp
    src/core/World.cpp
    src/core/Game.cpp
)
target_include_directories(dragon_core PUBLIC include)
if(MSVC)
    target_compile_options(dragon_core PUBLIC /utf-8 /W3 /permissive-)
else()
    target_compile_options(dragon_core PRIVATE -Wall -Wextra)
endif()

# ---- Windows 게임 실행 파일 ----
if(WIN32)
    add_executable(dragon_tower WIN32
        src/win/main.cpp
        src/win/App.cpp
        src/win/Renderer.cpp
        src/win/Sprites.cpp
        src/win/Platform.cpp
    )
    target_link_libraries(dragon_tower PRIVATE dragon_core gdiplus shell32 ole32 user32 gdi32)
    target_compile_definitions(dragon_tower PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
    # 실행 파일 옆에 assets 폴더를 복사한다(이미지를 넣으면 임시 도형 대신 표시됨).
    add_custom_command(TARGET dragon_tower POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:dragon_tower>/assets
    )
endif()

# ---- 테스트 ----
if(DRAGON_BUILD_TESTS)
    enable_testing()
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE dragon_core)
    add_test(NAME core_tests COMMAND core_tests)

    add_executable(balance_sim tests/balance_sim.cpp)
    target_compile_definitions(balance_sim PRIVATE DT_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
    target_link_libraries(balance_sim PRIVATE dragon_core)

    # 에셋 데이터가 제대로 읽히는지 확인한다(게임을 켜지 않고 오타·범위 오류를 잡는다)
    add_executable(asset_check tools/asset_check.cpp)
    target_link_libraries(asset_check PRIVATE dragon_core)
    add_test(NAME asset_check COMMAND asset_check "${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()

# ---- 퍼저 (사람이 쓰는 텍스트 파일 파서) ----
# 기본 꺼짐. Clang 에서만 켜진다 — MSVC·GCC 로 켜면 설정 단계에서 분명히 거절한다.
# 쓰는 법: tests/fuzz/README.md
option(DRAGON_BUILD_FUZZERS "파서 퍼저를 빌드한다 (Clang 전용)" OFF)
if(DRAGON_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "DRAGON_BUILD_FUZZERS 는 Clang 에서만 된다 (현재: ${CMAKE_CXX_COMPILER_ID}). "
                            "-DCMAKE_CXX_COMPILER=clang++ 를 함께 준다.")
    endif()
    # 퍼저는 커버리지 계측이 필요해 코어를 따로 빌드한다(일반 빌드에 영향 없음).
    add_library(dragon_core_fuzz STATIC
        src/core/Species.cpp
        src/core/Enemy.cpp
        src/core/DragonData.cpp
        src/core/Progression.cpp
        src/core/SaveData.cpp
        src/core/World.cpp
        src/core/Game.cpp
    )
    target_include_directories(dragon_core_fuzz PUBLIC include)
    target_compile_options(dragon_core_fuzz PUBLIC
        -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -g)
    target_link_options(dragon_core_fuzz PUBLIC -fsanitize=fuzzer-no-link,address,undefined)

    foreach(fz fuzz_dragon_text fuzz_enemy_text fuzz_save_roundtrip)
        add_executable(${fz} tests/fuzz/${fz}.cpp)
        target_link_libraries(${fz} PRIVATE dragon_core_fuzz)
        target_link_options(${fz} PRIVATE -fsanitize=fuzzer)
        # 모아 둔 입력을 다시 돌려 보는 회귀 검사.
        # **-runs=0 이 꼭 있어야 한다.** 폴더만 주면 libFuzzer 는 재생이 아니라
        # 무한 퍼징을 시작해서 ctest 가 영원히 끝나지 않는다(실제로 한 번 걸렸다).
        add_test(NAME ${fz}_corpus
                 COMMAND ${fz} -runs=0 "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/${fz}")
    endforeach()
endif()
