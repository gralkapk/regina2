project(particlevolume CXX)

find_package(glm CONFIG REQUIRED)

add_executable(particlevolume particlevolume.cpp)
target_link_libraries(particlevolume PRIVATE glm::glm-header-only)