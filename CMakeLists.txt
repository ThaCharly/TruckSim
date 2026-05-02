cmake_minimum_required(VERSION 3.15)
project(TruckSimulatorCore VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- CONFIGURACIÓN DE SFML ---
# Apagamos cosas que no nos interesan para acelerar la compilación
set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "No build SFML examples" FORCE)
set(SFML_BUILD_DOC OFF CACHE BOOL "No build SFML docs" FORCE)

# Compilar en estático para no tener que andar renegando con archivos .so o .dll
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)

include(FetchContent)

# Declaramos de dónde sacar SFML y qué versión exacta queremos
FetchContent_Declare(
    sfml
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG        2.5.1
)

# Descarga y configura SFML en este exacto momento
FetchContent_MakeAvailable(sfml)

find_package(SDL2 REQUIRED)

# --- CONFIGURACIÓN DEL PROYECTO ---
add_executable(TruckSim src/main.cpp)

# Linkeamos la versión estática/local que CMake acaba de construir.
# Usamos PRIVATE porque es la forma moderna de manejar dependencias en CMake.
target_link_libraries(TruckSim PRIVATE sfml-graphics sfml-window sfml-system sfml-audio SDL2::SDL2)