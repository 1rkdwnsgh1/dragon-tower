cmake_minimum_required(VERSION 3.20)
project(DragonTower LANGUAGES CXX)
add_executable(dragon_tower src/main.cpp)
target_compile_features(dragon_tower PRIVATE cxx_std_17)
