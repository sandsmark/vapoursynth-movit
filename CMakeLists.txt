project(vapoursynth-movit)
cmake_minimum_required(VERSION 3.18)
find_package(PkgConfig)
pkg_check_modules(Movit REQUIRED IMPORTED_TARGET "movit")
pkg_check_modules(Vapoursynth REQUIRED IMPORTED_TARGET "vapoursynth")
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET "egl")

add_library(vapoursynth-movit SHARED filter.cpp)
target_link_libraries(vapoursynth-movit PkgConfig::Movit PkgConfig::Vapoursynth PkgConfig::EGL)
install(TARGETS vapoursynth-movit LIBRARY DESTINATION lib/vapoursynth)

