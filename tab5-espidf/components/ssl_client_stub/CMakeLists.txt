# Deliberately has no REQUIRES on arduino-esp32 -- see ssl_client_stub.cpp's
# top comment for why that matters for link ordering.
idf_component_register(SRCS "ssl_client_stub.cpp")
