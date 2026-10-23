// Stub for arduino-esp32's NetworkClientSecure SSL backend
// (libraries/NetworkClientSecure/src/ssl_client.cpp).
//
// That file is listed in arduino-esp32's own ARDUINO_LIBRARY_NetworkClientSecure_SRCS
// and does get compiled (confirmed: the .cpp.obj exists, 79KB), but produces
// zero function symbols -- confirmed via `nm` on the real build output and by
// preprocessing it with this project's exact compile flags. The real cause
// was not chased further than confirming it's a config/preprocessor-guard
// issue internal to that file (it emits a #warning about enabling PSK
// ciphersuites in mbedTLS menuconfig, suggesting a real but out-of-scope
// mbedTLS Kconfig gap), not a missing-library problem like everything else
// this port has hit.
//
// SshService/HttpService (SSH and HTTPS client modes) are not the goal of
// this real-dispatcher attempt -- constructing DependencyProvider/
// ActionDispatcher without crashing is. Stubbing these to a clean failure
// (matching this project's established pattern for USB device-mode HID/MSC,
// also genuinely blocked by an upstream version gap rather than faked) lets
// the real target compile and link, while being honest that SSH/HTTPS
// actually making a TLS connection remains unimplemented, not silently
// broken.
//
// Lives in its OWN component (not in main/) deliberately: arduino-esp32's
// NetworkClientSecure.cpp (needs these symbols) is linked via a two-pass
// repeated-archive group that runs entirely BEFORE libmain.a is ever
// reached a second time, so a definition sitting in main's archive never
// gets pulled into the pass that actually needs it (confirmed by reading
// the generated link.txt: libmain.a appears exactly once, before
// arduino-esp32's repeated second pass). Making arduino-esp32 PRIV_REQUIRES
// this component instead lets CMake's normal topological component-link
// ordering place it correctly, with no circular dependency (this component
// does not REQUIRES arduino-esp32 back -- it only forward-declares the two
// parameter types, deliberately, so it has zero real dependency on it).
#include <cstddef>
#include <cstdint>

// Plain C++ linkage deliberately -- the real ssl_client.h declares these
// with no `extern "C"` at all, so these must produce the exact same
// mangled names. Forward-declaring the two parameter types is sufficient
// for that (Itanium C++ ABI mangling depends only on the qualified type
// name, not its full definition).
struct sslclient_context;
class IPAddress;

void ssl_init(sslclient_context*) {}

// port param matches the real header's `uint32_t`, written as `unsigned long`
// here deliberately -- this toolchain's uint32_t resolves to `unsigned long`
// (confirmed by reading the actual mangled undefined-reference signature
// from the linker), and the mangled name must match exactly.
int start_ssl_client(sslclient_context*, const IPAddress&, unsigned long, const char*, int,
                      const char*, bool, const char*, const char*, const char*, const char*,
                      bool, const char**) {
    return -1;  // genuine failure, not a silent success
}

int ssl_starttls_handshake(sslclient_context*) { return -1; }
void stop_ssl_socket(sslclient_context*) {}
int data_to_read(sslclient_context*) { return 0; }
int send_ssl_data(sslclient_context*, const uint8_t*, size_t) { return -1; }
int get_ssl_receive(sslclient_context*, uint8_t*, int) { return -1; }
int send_net_data(sslclient_context*, const uint8_t*, size_t) { return -1; }
int get_net_receive(sslclient_context*, uint8_t*, int) { return -1; }
int peek_net_receive(sslclient_context*, int) { return -1; }
bool verify_ssl_fingerprint(sslclient_context*, const char*, const char*) { return false; }
bool verify_ssl_dn(sslclient_context*, const char*) { return false; }
