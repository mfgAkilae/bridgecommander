/* Socket layer hooks: loopback binding and network condition emulation (test mode),
 * traffic statistics (diagnostics). The game imports WSOCK32 by ordinal. */
#include "common.h"
#include <stdlib.h>
#include <string.h>

#define ORD_BIND 2
#define ORD_RECVFROM 17
#define ORD_SENDTO 20

typedef int(WINAPI *PFN_bind)(SOCKET, const struct sockaddr *, int);
typedef int(WINAPI *PFN_sendto)(SOCKET, const char *, int, int, const struct sockaddr *, int);
typedef int(WINAPI *PFN_recvfrom)(SOCKET, char *, int, int, struct sockaddr *, int *);

static PFN_bind o_bind;
static PFN_sendto o_sendto;
static PFN_recvfrom o_recvfrom;
static CRITICAL_SECTION g_netLock;
static ULONG g_bindAddr; /* network order, 0 = no override */

/* ---- emulator queue ------------------------------------------------------ */
typedef struct Pkt {
    struct Pkt *next;
    double due;
    SOCKET s;
    struct sockaddr_in to;
    int len;
    char data[1];
} Pkt;
static Pkt *g_queue; /* sorted by due */

static struct {
    unsigned long outPkts, outBytes, inPkts, inBytes, dropped;
    double last;
} g_stats;

static int emu_active(void)
{
    return g_cfg.testMode && (g_cfg.emuLatencyMs > 0 || g_cfg.emuJitterMs > 0 || g_cfg.emuLossPct10 > 0);
}

static void pump(void)
{
    double t = now_sec();
    EnterCriticalSection(&g_netLock);
    while (g_queue && g_queue->due <= t) {
        Pkt *p = g_queue;
        g_queue = p->next;
        o_sendto(p->s, p->data, p->len, 0, (struct sockaddr *)&p->to, sizeof(p->to));
        free(p);
    }
    if (g_cfg.statsIntervalSec > 0 && t - g_stats.last >= g_cfg.statsIntervalSec) {
        double dt = t - g_stats.last;
        if (g_stats.last > 0)
            logf("NET out %.1f pkt/s %.1f kB/s | in %.1f pkt/s %.1f kB/s | emu-dropped %lu",
                 g_stats.outPkts / dt, g_stats.outBytes / dt / 1024.0, g_stats.inPkts / dt,
                 g_stats.inBytes / dt / 1024.0, g_stats.dropped);
        g_stats.outPkts = g_stats.outBytes = g_stats.inPkts = g_stats.inBytes = g_stats.dropped = 0;
        g_stats.last = t;
    }
    LeaveCriticalSection(&g_netLock);
}

static void ensure_bound(SOCKET s)
{
    struct sockaddr_in a;
    int al = sizeof(a);
    if (!g_bindAddr)
        return;
    if (getsockname(s, (struct sockaddr *)&a, &al) == 0)
        return; /* already bound */
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = g_bindAddr;
    a.sin_port = htons((u_short)g_cfg.clientBindPort);
    if (o_bind(s, (struct sockaddr *)&a, sizeof(a)) != 0 && g_cfg.clientBindPort) {
        a.sin_port = 0; /* port taken (e.g. GameSpy socket) - fall back to ephemeral */
        o_bind(s, (struct sockaddr *)&a, sizeof(a));
    }
    al = sizeof(a);
    getsockname(s, (struct sockaddr *)&a, &al);
    logf("implicit bind socket %u -> %s:%u", (unsigned)s, g_cfg.bindAddress, ntohs(a.sin_port));
}

static int WINAPI hk_bind(SOCKET s, const struct sockaddr *addr, int len)
{
    struct sockaddr_in a;
    if (g_bindAddr && addr && len >= (int)sizeof(a) && addr->sa_family == AF_INET) {
        memcpy(&a, addr, sizeof(a));
        if (a.sin_addr.s_addr == INADDR_ANY) {
            a.sin_addr.s_addr = g_bindAddr;
            logf("bind override -> %s:%u", g_cfg.bindAddress, ntohs(a.sin_port));
            return o_bind(s, (struct sockaddr *)&a, sizeof(a));
        }
    }
    return o_bind(s, addr, len);
}

/* log each new (direction, socket, address) once, for connection debugging */
static void note_peer(const char *dir, SOCKET s, const struct sockaddr *a, int len, int r)
{
    static struct { char dir; SOCKET s; ULONG ip; USHORT port; } seen[32];
    static int nseen;
    const struct sockaddr_in *in = (const struct sockaddr_in *)a;
    int i;
    if (!a || in->sin_family != AF_INET)
        return;
    for (i = 0; i < nseen; i++)
        if (seen[i].dir == dir[0] && seen[i].s == s && seen[i].ip == in->sin_addr.s_addr && seen[i].port == in->sin_port)
            return;
    if (nseen < 32) {
        seen[nseen].dir = dir[0];
        seen[nseen].s = s;
        seen[nseen].ip = in->sin_addr.s_addr;
        seen[nseen].port = in->sin_port;
        nseen++;
        logf("peer %s socket %u %s:%u len=%d result=%d err=%d", dir, (unsigned)s, inet_ntoa(in->sin_addr),
             ntohs(in->sin_port), len, r, r < 0 ? WSAGetLastError() : 0);
    }
}

static int WINAPI hk_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
    ensure_bound(s);
    if (g_cfg.verbose)
        note_peer("send", s, to, len, 0);
    g_stats.outPkts++;
    g_stats.outBytes += len;
    if (emu_active() && to && tolen >= (int)sizeof(struct sockaddr_in)) {
        Pkt *p, **pp;
        double lat;
        if (g_cfg.emuLossPct10 > 0 && (rand() % 1000) < g_cfg.emuLossPct10) {
            g_stats.dropped++;
            pump();
            return len;
        }
        lat = g_cfg.emuLatencyMs / 1000.0;
        if (g_cfg.emuJitterMs > 0)
            lat += ((rand() % (2 * g_cfg.emuJitterMs + 1)) - g_cfg.emuJitterMs) / 1000.0;
        if (lat < 0)
            lat = 0;
        p = (Pkt *)malloc(sizeof(Pkt) + len);
        p->due = now_sec() + lat;
        p->s = s;
        memcpy(&p->to, to, sizeof(p->to));
        p->len = len;
        memcpy(p->data, buf, len);
        EnterCriticalSection(&g_netLock);
        pp = &g_queue;
        while (*pp && (*pp)->due <= p->due)
            pp = &(*pp)->next;
        p->next = *pp;
        *pp = p;
        LeaveCriticalSection(&g_netLock);
        pump();
        return len;
    }
    pump();
    return o_sendto(s, buf, len, flags, to, tolen);
}

static int WINAPI hk_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
    int r;
    pump();
    r = o_recvfrom(s, buf, len, flags, from, fromlen);
    if (r > 0) {
        g_stats.inPkts++;
        g_stats.inBytes += r;
        if (g_cfg.verbose)
            note_peer("recv", s, from, r, r);
    } else if (r < 0 && g_cfg.verbose) {
        int e = WSAGetLastError();
        static int nerr;
        if (e != WSAEWOULDBLOCK && nerr++ < 10)
            logf("recvfrom socket %u error %d", (unsigned)s, e);
        WSASetLastError(e);
    }
    return r;
}

typedef struct hostent *(WINAPI *PFN_gethostbyname)(const char *);
static PFN_gethostbyname o_gethostbyname;
static struct hostent g_fakeHost;
static char *g_fakeAddrList[2];
static ULONG g_fakeAddr;

/* The game resolves its own host name to find "its" IP and refuses to join that
 * IP; it also announces it to the host. In test mode the own name resolves to the
 * configured bind address so loopback sessions work without a network adapter. */
static struct hostent *WINAPI hk_gethostbyname(const char *name)
{
    struct hostent *h;
    char me[256];
    if (g_bindAddr && name && gethostname(me, sizeof(me)) == 0 && _stricmp(me, name) == 0) {
        g_fakeAddr = g_bindAddr;
        g_fakeAddrList[0] = (char *)&g_fakeAddr;
        g_fakeAddrList[1] = NULL;
        g_fakeHost.h_name = (char *)name;
        g_fakeHost.h_aliases = NULL;
        g_fakeHost.h_addrtype = AF_INET;
        g_fakeHost.h_length = 4;
        g_fakeHost.h_addr_list = g_fakeAddrList;
        logf("gethostbyname(%s) -> %s (test override)", name, g_cfg.bindAddress);
        return &g_fakeHost;
    }
    h = o_gethostbyname(name);
    if (g_cfg.verbose) {
        if (h && h->h_addr_list && h->h_addr_list[0])
            logf("gethostbyname(%s) -> %s", name ? name : "", inet_ntoa(*(struct in_addr *)h->h_addr_list[0]));
        else
            logf("gethostbyname(%s) -> fail %d", name ? name : "", WSAGetLastError());
    }
    return h;
}

void net_install(void)
{
    HMODULE exe = GetModuleHandleA(NULL);
    InitializeCriticalSection(&g_netLock);
    srand(GetTickCount() ^ GetCurrentProcessId());
    if (g_cfg.testMode && g_cfg.bindAddress[0])
        g_bindAddr = inet_addr(g_cfg.bindAddress);
    if (!(g_cfg.testMode || g_cfg.statsIntervalSec > 0))
        return;
    iat_hook(exe, "WSOCK32.dll", NULL, ORD_BIND, (void *)hk_bind, (void **)&o_bind);
    iat_hook(exe, "WSOCK32.dll", NULL, ORD_SENDTO, (void *)hk_sendto, (void **)&o_sendto);
    iat_hook(exe, "WSOCK32.dll", NULL, ORD_RECVFROM, (void *)hk_recvfrom, (void **)&o_recvfrom);
    iat_hook(exe, "WSOCK32.dll", NULL, 52, (void *)hk_gethostbyname, (void **)&o_gethostbyname);
    logf("socket hooks installed");
}
