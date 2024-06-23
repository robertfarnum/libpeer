
set(COREHTTP_CMAKE_FILE ${CMAKE_SOURCE_DIR}/../../third_party/coreHTTP/httpFilePaths.cmake)
set(PEER_PROJECT_PATH "../../../..")

if (EXISTS ${COREHTTP_CMAKE_FILE})
  include(${COREHTTP_CMAKE_FILE})
endif()

file(GLOB CODES "${PEER_PROJECT_PATH}/src/*.c")

idf_component_register(
  SRCS ${CODES} ${HTTP_SOURCES}
  INCLUDE_DIRS "${PEER_PROJECT_PATH}/src" ${HTTP_INCLUDE_PUBLIC_DIRS}
  REQUIRES mbedtls srtp json mdns mqtt
)

add_definitions("-DESP32 -DHTTP_DO_NOT_USE_CUSTOM_CONFIG -DMQTT_DO_NOT_USE_CUSTOM_CONFIG")

