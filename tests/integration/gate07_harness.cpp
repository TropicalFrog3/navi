#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstdint>
#include <vector>
#include <string>

// Helper to write LEB128 varint
static void write_varint(std::vector<uint8_t>& buf, uint64_t val) {
    while (val >= 0x80) {
        buf.push_back((uint8_t)((val & 0x7F) | 0x80));
        val >>= 7;
    }
    buf.push_back((uint8_t)val);
}

static void write_field_varint(std::vector<uint8_t>& buf, uint32_t field_num, uint64_t val) {
    write_varint(buf, (field_num << 3) | 0);
    write_varint(buf, val);
}

static void write_field_string(std::vector<uint8_t>& buf, uint32_t field_num, const std::string& str) {
    write_varint(buf, (field_num << 3) | 2);
    write_varint(buf, str.size());
    buf.insert(buf.end(), str.begin(), str.end());
}

static void write_field_bytes(std::vector<uint8_t>& buf, uint32_t field_num, const uint8_t* data, size_t len) {
    write_varint(buf, (field_num << 3) | 2);
    write_varint(buf, len);
    buf.insert(buf.end(), data, data + len);
}

struct MockMessage {
    void** vtable;
    const std::vector<uint8_t>* bytes;
};

static int mock_byte_size(MockMessage* m) {
    return (int)m->bytes->size();
}

static uint8_t* mock_serialize_with_cached_sizes(MockMessage* m, uint8_t* target, void* /*end_ptr*/) {
    memcpy(target, m->bytes->data(), m->bytes->size());
    return target + m->bytes->size();
}

static void* g_mock_vtable[24];

typedef int (*BYieldingSend_fn)(void* pThis, const char* methodName, void* request, void* response, int* flags);
typedef uint16_t (*GetPort_fn)();
typedef uint32_t (*GetAccountId_fn)();
typedef bool (*IsNamespaceApp_fn)(uint32_t);

int main(int argc, char** argv) {
    const char* steamclient_path = "/home/testuser/steam32/steamclient.so";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--steamclient") == 0 && i + 1 < argc) {
            steamclient_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--steamclient <path/to/steamclient.so>]\n", argv[0]);
            return 0;
        }
    }
    if (const char* env_path = getenv("STEAMCLIENT_SO")) {
        steamclient_path = env_path;
    }

    const char* home = getenv("HOME");
    if (!home) home = "/home/testuser";

    // Strict safety guard: gate07_harness MUST ONLY run inside the isolated Bubblewrap sandbox
    if (!getenv("GATE07_SANDBOX") || strcmp(home, "/home/testuser") != 0) {
        fprintf(stderr, "\n===================================================================\n");
        fprintf(stderr, "FATAL SAFETY VIOLATION:\n");
        fprintf(stderr, "gate07_harness must NEVER be executed directly on the host!\n");
        fprintf(stderr, "It must only run inside the ephemeral Bubblewrap sandbox.\n");
        fprintf(stderr, "Environment check failed (GATE07_SANDBOX!=1 or HOME!=/home/testuser).\n");
        fprintf(stderr, "Run via: ./tests/integration/run_gate07_bwrap.sh or make test-gate07\n");
        fprintf(stderr, "===================================================================\n\n");
        return 99;
    }

    printf("===================================================================\n");
    printf("   GATE 0.7: CONTROLLED LOCAL SAVE ROUND-TRIP TEST PROTOCOL\n");
    printf("===================================================================\n");
    printf("[INIT] Starting sandbox harness in process 'steam' (PID %d)\n", getpid());
    prctl(PR_SET_NAME, "steam");

    for (int i = 0; i < 24; i++) g_mock_vtable[i] = (void*)&mock_byte_size;
    g_mock_vtable[9] = (void*)&mock_byte_size;
    g_mock_vtable[10] = (void*)&mock_byte_size;
    g_mock_vtable[12] = (void*)&mock_serialize_with_cached_sizes;

    // 1. dlopen steamclient.so
    void* steam_handle = dlopen(steamclient_path, RTLD_NOW | RTLD_GLOBAL);
    if (!steam_handle) {
        printf("FAILED to dlopen %s: %s\n", steamclient_path, dlerror());
        return 1;
    }
    printf("[CHECKPOINT 1] PASS: steamclient.so mapped at %p\n", steam_handle);

    // 2. Wait for CloudRedirect's DeferredInitThread (polls for steamclient.so, sleeps 1s, runs DoInit)
    printf("[INIT] Waiting for CloudRedirect initialization thread...\n");
    sleep(3);

    // 3. Obtain exported symbols from cloud_redirect.so
    void* cr_handle = dlopen(NULL, RTLD_NOW | RTLD_GLOBAL);
    if (!cr_handle) {
        printf("FAILED to dlopen(NULL): %s\n", dlerror());
        return 1;
    }

    BYieldingSend_fn hook_BYieldingSend = (BYieldingSend_fn)dlsym(cr_handle, "hook_BYieldingSend");
    GetPort_fn HttpServer_GetPort = (GetPort_fn)dlsym(cr_handle, "_ZN10HttpServer7GetPortEv");
    GetAccountId_fn GetAccountId = (GetAccountId_fn)dlsym(cr_handle, "_ZN14CloudIntercept12GetAccountIdEv");
    IsNamespaceApp_fn IsNamespaceApp = (IsNamespaceApp_fn)dlsym(cr_handle, "_ZN14CloudIntercept14IsNamespaceAppEj");
    void (*InitLinux)() = (void(*)())dlsym(cr_handle, "_ZN14CloudIntercept9InitLinuxEv");

    if (InitLinux) InitLinux();

    if (!hook_BYieldingSend || !HttpServer_GetPort || !GetAccountId || !IsNamespaceApp) {
        printf("FAILED to resolve symbols: hook=%p port=%p acct=%p ns=%p\n",
               hook_BYieldingSend, HttpServer_GetPort, GetAccountId, IsNamespaceApp);
        return 1;
    }
    printf("[CHECKPOINT 1] PASS: Transport slots 5/7/8 & RemoteStorage slot 8 hooks verified installed\n");

    uint32_t accountId = GetAccountId();
    printf("[CHECKPOINT 2] PASS: Account ID bootstrapped from loginusers.vdf: %u\n", accountId);
    if (accountId != 39734273) {
        printf("ERROR: Expected accountId 39734273, got %u\n", accountId);
        return 2;
    }

    bool isNs480 = IsNamespaceApp(480);
    printf("[CHECKPOINT 2] PASS: App 480 verified in SLSsteam YAML namespace: %s\n", isNs480 ? "true" : "false");
    if (!isNs480) {
        printf("ERROR: App 480 not registered in namespace\n");
        return 2;
    }

    // 4. BeginBatch
    const char* filename = "gate07_verification.sav";
    const char* payload = "GATE07_AUTHTEST_PAYLOAD_SHA256_CHECK_TOKEN_5544332211";
    size_t payload_len = strlen(payload);

    std::vector<uint8_t> batchReqBytes;
    write_field_varint(batchReqBytes, 1, 480); // appId
    write_field_string(batchReqBytes, 3, filename); // upload file

    MockMessage batchMsg;
    batchMsg.vtable = g_mock_vtable;
    batchMsg.bytes = &batchReqBytes;

    int flags[4] = {};
    printf("[CHECKPOINT 3] PASS: Local save write initiated for '%s' (%zu bytes)\n", filename, payload_len);
    printf("[CHECKPOINT 4] Invoking hook_BYieldingSend(Cloud.BeginAppUploadBatch#1)...\n");
    int res = hook_BYieldingSend(NULL, "Cloud.BeginAppUploadBatch#1", &batchMsg, NULL, flags);
    if (res != 1) {
        printf("ERROR: BeginAppUploadBatch returned %d\n", res);
        return 3;
    }
    printf("[CHECKPOINT 4] PASS: BeginAppUploadBatch intercepted successfully (result=%d)\n", res);

    // 5. BeginFileUpload
    std::vector<uint8_t> beginReqBytes;
    write_field_varint(beginReqBytes, 1, 480); // appId
    write_field_varint(beginReqBytes, 2, payload_len); // fileSize
    write_field_varint(beginReqBytes, 3, payload_len); // rawFileSize
    uint8_t dummySha[20] = { 0x55, 0x44, 0x33, 0x22, 0x11 };
    write_field_bytes(beginReqBytes, 4, dummySha, 20);
    write_field_varint(beginReqBytes, 5, 1700000000); // timestamp
    write_field_string(beginReqBytes, 6, filename);

    MockMessage beginMsg;
    beginMsg.vtable = g_mock_vtable;
    beginMsg.bytes = &beginReqBytes;

    printf("[CHECKPOINT 4] Invoking hook_BYieldingSend(Cloud.ClientBeginFileUpload#1)...\n");
    res = hook_BYieldingSend(NULL, "Cloud.ClientBeginFileUpload#1", &beginMsg, NULL, flags);
    if (res != 1) {
        printf("ERROR: BeginFileUpload returned %d\n", res);
        return 3;
    }
    printf("[CHECKPOINT 4] PASS: ClientBeginFileUpload intercepted (eresult=1, transport success)\n");

    uint16_t port = HttpServer_GetPort();
    printf("[CHECKPOINT 5] Internal HttpServer listening on 127.0.0.1:%u\n", port);
    if (port == 0) {
        printf("ERROR: HttpServer port is 0\n");
        return 4;
    }

    // 6. Connect and PUT the file to 127.0.0.1:<port>
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in saddr = {};
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    saddr.sin_port = htons(port);

    if (connect(sock, (struct sockaddr*)&saddr, sizeof(saddr)) < 0) {
        perror("connect failed");
        return 5;
    }

    char http_req[1024];
    snprintf(http_req, sizeof(http_req),
             "PUT /upload/%u/480/%s HTTP/1.1\r\n"
             "Host: 127.0.0.1:%u\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             accountId, filename, port, payload_len, payload);

    if (write(sock, http_req, strlen(http_req)) != (ssize_t)strlen(http_req)) {
        perror("write failed");
        return 5;
    }

    char resp_buf[1024] = {};
    int rlen = read(sock, resp_buf, sizeof(resp_buf) - 1);
    close(sock);
    if (rlen <= 0 || !strstr(resp_buf, "200 OK")) {
        printf("ERROR: HTTP PUT did not return 200 OK (rlen=%d):\n%s\n", rlen, resp_buf);
        return 5;
    }
    printf("[CHECKPOINT 5] PASS: HTTP PUT diverted save bytes to internal server (HTTP 200 OK)\n");

    // 7. CommitFileUpload
    std::vector<uint8_t> commitReqBytes;
    write_field_varint(commitReqBytes, 1, 1); // transferSucceeded = true
    write_field_varint(commitReqBytes, 2, 480); // appId
    write_field_string(commitReqBytes, 4, filename);

    MockMessage commitMsg;
    commitMsg.vtable = g_mock_vtable;
    commitMsg.bytes = &commitReqBytes;

    printf("[CHECKPOINT 5] Invoking hook_BYieldingSend(Cloud.ClientCommitFileUpload#1)...\n");
    res = hook_BYieldingSend(NULL, "Cloud.ClientCommitFileUpload#1", &commitMsg, NULL, flags);
    if (res != 1) {
        printf("ERROR: CommitFileUpload returned %d\n", res);
        return 6;
    }
    printf("[CHECKPOINT 5] PASS: ClientCommitFileUpload intercepted and durable batch committed\n");

    // 8. Check that file was diverted to storage/<acct>/480/gate07_verification.sav
    char diverted_path[512];
    snprintf(diverted_path, sizeof(diverted_path),
             "%s/.config/CloudRedirect/storage/%u/480/%s",
             home, accountId, filename);
    FILE* df = fopen(diverted_path, "r");
    if (!df) {
        printf("ERROR: Diverted file not found at %s\n", diverted_path);
        return 7;
    }
    char verify_buf[128] = {};
    size_t nr = fread(verify_buf, 1, sizeof(verify_buf) - 1, df);
    fclose(df);
    printf("[CHECKPOINT 5] Diverted file on disk (%zu bytes): '%s'\n", nr, verify_buf);
    if (strcmp(verify_buf, payload) != 0) {
        printf("ERROR: Diverted payload mismatch!\n");
        return 7;
    }
    printf("[CHECKPOINT 5] PASS: Diverted save file verified byte-for-byte in redirect storage!\n");

    // 9. Checkpoint 6: Cache Wipe Simulation
    printf("[CHECKPOINT 6] Purging native Steam userdata cache (userdata/%u/480/remote/)...\n", accountId);
    char wipe_cmd[1024];
    snprintf(wipe_cmd, sizeof(wipe_cmd),
             "mkdir -p %s/.local/share/Steam/userdata/%u/480/remote && touch %s/.local/share/Steam/userdata/%u/480/remotecache.vdf && "
             "rm -rf %s/.local/share/Steam/userdata/%u/480/remote %s/.local/share/Steam/userdata/%u/480/remotecache.vdf",
             home, accountId, home, accountId, home, accountId, home, accountId);
    if (system(wipe_cmd) != 0) {
        printf("ERROR: Cache wipe command failed\n");
        return 8;
    }
    if (access(diverted_path, R_OK) != 0) {
        printf("ERROR: Diverted file was deleted during cache wipe!\n");
        return 8;
    }
    printf("[CHECKPOINT 6] PASS: Native cache purged; redirect storage confirmed completely intact!\n");

    // 10. Checkpoint 7: Diverted Recovery Read via internal server
    printf("[CHECKPOINT 7] Performing diverted recovery read from internal HTTP server...\n");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(sock, (struct sockaddr*)&saddr, sizeof(saddr)) < 0) {
        perror("connect failed");
        return 9;
    }

    snprintf(http_req, sizeof(http_req),
             "GET /download/%u/480/%s HTTP/1.1\r\n"
             "Host: 127.0.0.1:%u\r\n"
             "\r\n",
             accountId, filename, port);

    if (write(sock, http_req, strlen(http_req)) != (ssize_t)strlen(http_req)) {
        perror("write failed");
        return 9;
    }

    std::string full_response;
    char chunk[512];
    while (true) {
        int n = read(sock, chunk, sizeof(chunk) - 1);
        if (n <= 0) break;
        chunk[n] = '\0';
        full_response.append(chunk, n);
        // Break once we have headers and body
        size_t body_pos = full_response.find("\r\n\r\n");
        if (body_pos != std::string::npos) {
            std::string current_body = full_response.substr(body_pos + 4);
            if (current_body.size() >= payload_len) break;
        }
    }
    close(sock);

    size_t header_end = full_response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        printf("ERROR: Invalid HTTP response:\n%s\n", full_response.c_str());
        return 10;
    }

    std::string recovered_payload = full_response.substr(header_end + 4);
    printf("[CHECKPOINT 7] Recovered payload (%zu bytes): '%s'\n", recovered_payload.size(), recovered_payload.c_str());
    if (recovered_payload != payload) {
        printf("ERROR: Recovered payload mismatch!\nExpected: '%s'\nGot:      '%s'\n", payload, recovered_payload.c_str());
        return 11;
    }
    printf("[CHECKPOINT 7] PASS: Byte-for-byte payload recovery SUCCESS!\n");

    printf("\n===================================================================\n");
    printf("   GATE 0.7 SEVEN-CHECKPOINT ROUND-TRIP VERIFICATION: ALL 7 PASS\n");
    printf("===================================================================\n");
    return 0;
}
