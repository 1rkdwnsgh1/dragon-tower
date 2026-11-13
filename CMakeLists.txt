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
    target_link_libraries(balance_sim PRIVATE dragon_core)
endif()
