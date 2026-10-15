/* Direct phaser reports (DirectPhaserReports, issue #23).
 *
 * The game's network is a star: clients exchange packets only with the host. A client's
 * phaser bank report (PhaserHitAuthority) therefore reached the other clients through the
 * host, one latency later than the host, as a reliable game message that waits for the
 * next send of the game's network layer on both hops.
 *
 * Here the DLL also sends every report itself, at once, as a small UDP packet on the game's
 * socket to every other machine that runs bcmpfix: the host learns the modded clients from
 * their HELLO packets and tells each the others' addresses (PEERS); clients send HELLO to
 * each other too, which opens NAT mappings and stateful firewalls for the reverse direction.
 * The reliable report through the host stays: a sequence number per sender lets the first
 * copy win. Packets start with a magic that the game never sees (net.c filters them out of
 * recvfrom). They go only to machines that announced they take them: a client sends HELLO
 * only after the host's script message MSG_DIRECT, the host and the clients send only to the
 * clients that sent HELLO (PEERS), never to unmodded or older machines. */
#include "common.h"
#include <string.h>

#define MAGIC "BCMPFX"
#define MAGIC_LEN 6
#define VERSION 1
#define PK_HELLO 1
#define PK_PEERS 2
#define PK_BANKS 3
#define MAXEP 16
#define EP_TIMEOUT 6.0  /* s without HELLO: the machine left */
#define HELLO_EVERY 1.0 /* s */

typedef struct {
    ULONG ip;     /* network order */
    USHORT port;  /* network order */
    double seen;
} Endpoint;

static int g_role;           /* 0 off, 1 host, 2 client of a modded host */
static SOCKET g_sock = INVALID_SOCKET;
static Endpoint g_host;      /* client: where the game sends its packets */
static Endpoint g_ep[MAXEP]; /* host: modded clients (HELLO); client: other modded clients (PEERS) */
static int g_nep;
static double g_nextHello, g_nextPeers;
static int g_sending;        /* our own sendto, not the game's */
static CRITICAL_SECTION g_lock;
static int g_lockInit;
static struct {
    DWORD v[6]; /* ship, seq, bits, target, hits, on */
} g_rx[64];
static int g_nrx;
static unsigned long g_txBanks, g_rxBanks, g_rxHello, g_rxPeers;

int direct_enabled(void)
{
    return g_cfg.directReports && g_cfg.phaserSync >= 3 && g_cfg.mpScript;
}

void direct_init(void)
{
    if (!g_lockInit) {
        InitializeCriticalSection(&g_lock);
        g_lockInit = 1;
    }
}

/* script, every second: our role in the game. hostDirect: the host announced that it takes
 * these packets (MSG_DIRECT); older versions would pass them to the game's network layer. */
void direct_set_role(int isHost, int hostDirect)
{
    int role = !direct_enabled() ? 0 : isHost ? 1 : (hostDirect ? 2 : 0);
    if (role != g_role)
        logf("direct phaser reports: %s", role == 1 ? "host" : role == 2 ? "client" : "off");
    g_role = role;
}

/* net.c: a packet the game sends; remember its socket and, on a client, the host's address */
void direct_note_game_send(SOCKET s, const struct sockaddr *to)
{
    const struct sockaddr_in *a = (const struct sockaddr_in *)to;
    if (g_sending || !a || a->sin_family != AF_INET || a->sin_addr.s_addr == INADDR_BROADCAST)
        return;
    g_sock = s;
    if (g_role != 1) {
        g_host.ip = a->sin_addr.s_addr;
        g_host.port = a->sin_port;
    }
}

static void send_to(const Endpoint *e, const char *buf, int len)
{
    struct sockaddr_in a;
    if (g_sock == INVALID_SOCKET || !e->ip)
        return;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = e->ip;
    a.sin_port = e->port;
    g_sending = 1;
    net_sendto(g_sock, buf, len, (struct sockaddr *)&a, sizeof(a));
    g_sending = 0;
}

static int header(char *buf, int type)
{
    memcpy(buf, MAGIC, MAGIC_LEN);
    buf[MAGIC_LEN] = VERSION;
    buf[MAGIC_LEN + 1] = (char)type;
    return MAGIC_LEN + 2;
}

static int find_ep(ULONG ip, USHORT port)
{
    int i;
    for (i = 0; i < g_nep; i++)
        if (g_ep[i].ip == ip && g_ep[i].port == port)
            return i;
    return -1;
}

static int is_host(ULONG ip, USHORT port)
{
    return g_host.ip == ip && g_host.port == port;
}

/* net.c: a received packet; returns 1 if it was ours (the game must not see it) */
int direct_filter(SOCKET s, const char *buf, int len, const struct sockaddr *from)
{
    const struct sockaddr_in *a = (const struct sockaddr_in *)from;
    int type, i;
    if (len < MAGIC_LEN + 2 || memcmp(buf, MAGIC, MAGIC_LEN) != 0 || buf[MAGIC_LEN] != VERSION)
        return 0;
    type = buf[MAGIC_LEN + 1];
    if (!a || a->sin_family != AF_INET || !g_role)
        return 1;
    EnterCriticalSection(&g_lock);
    if (type == PK_HELLO && len == MAGIC_LEN + 2) {
        g_rxHello++;
        if (g_role == 1) {
            /* host: a modded client */
            i = find_ep(a->sin_addr.s_addr, a->sin_port);
            if (i < 0 && g_nep < MAXEP) {
                i = g_nep++;
                g_ep[i].ip = a->sin_addr.s_addr;
                g_ep[i].port = a->sin_port;
                g_nextPeers = 0; /* tell everybody at once */
                logf("direct phaser reports: client %s:%u", inet_ntoa(a->sin_addr), ntohs(a->sin_port));
            }
            if (i >= 0)
                g_ep[i].seen = now_sec();
        }
        (void)s;
    } else if (type == PK_PEERS && g_role == 2 && is_host(a->sin_addr.s_addr, a->sin_port) &&
               len >= MAGIC_LEN + 3 && len == MAGIC_LEN + 3 + 6 * (BYTE)buf[MAGIC_LEN + 2]) {
        int n = (BYTE)buf[MAGIC_LEN + 2], k, added = 0;
        Endpoint old[MAXEP];
        int nold = g_nep;
        g_rxPeers++;
        memcpy(old, g_ep, sizeof(old));
        g_nep = 0;
        for (k = 0; k < n && g_nep < MAXEP; k++) {
            const char *p = buf + MAGIC_LEN + 3 + 6 * k;
            Endpoint e;
            int j;
            memcpy(&e.ip, p, 4);
            memcpy(&e.port, p + 4, 2);
            e.seen = now_sec();
            g_ep[g_nep++] = e;
            for (j = 0; j < nold; j++)
                if (old[j].ip == e.ip && old[j].port == e.port)
                    break;
            if (j == nold) {
                struct in_addr ia;
                ia.s_addr = e.ip;
                logf("direct phaser reports: peer %s:%u", inet_ntoa(ia), ntohs(e.port));
                added = 1;
            }
        }
        if (added)
            g_nextHello = 0; /* open the paths to the new peers at once */
    } else if (type == PK_BANKS && len == MAGIC_LEN + 2 + 24) {
        /* only from machines we know run bcmpfix */
        int known = g_role == 1 ? find_ep(a->sin_addr.s_addr, a->sin_port) >= 0
                                : (is_host(a->sin_addr.s_addr, a->sin_port) || find_ep(a->sin_addr.s_addr, a->sin_port) >= 0);
        if (known && g_nrx < 64) {
            memcpy(g_rx[g_nrx].v, buf + MAGIC_LEN + 2, 24);
            g_nrx++;
            g_rxBanks++;
        }
    }
    LeaveCriticalSection(&g_lock);
    return 1;
}

/* phaser.c: our own ship's banks changed; send the report to every modded machine */
void direct_send_banks(DWORD ship, DWORD seq, DWORD bits, DWORD target, DWORD hits, DWORD on)
{
    char buf[MAGIC_LEN + 2 + 24];
    DWORD v[6];
    int n, i;
    if (!g_role || g_sock == INVALID_SOCKET || !g_lockInit)
        return;
    n = header(buf, PK_BANKS);
    v[0] = ship;
    v[1] = seq;
    v[2] = bits;
    v[3] = target;
    v[4] = hits;
    v[5] = on;
    memcpy(buf + n, v, 24);
    EnterCriticalSection(&g_lock);
    if (g_role == 2)
        send_to(&g_host, buf, sizeof(buf));
    for (i = 0; i < g_nep; i++)
        send_to(&g_ep[i], buf, sizeof(buf));
    LeaveCriticalSection(&g_lock);
    g_txBanks++;
}

/* every frame (main thread): apply received reports, HELLO and PEERS */
void direct_frame(void)
{
    double t = now_sec();
    int i, n;
    if (!g_lockInit)
        return;
    EnterCriticalSection(&g_lock);
    n = g_nrx;
    for (i = 0; i < n; i++)
        phaser_set_remote_seq(g_rx[i].v[0], g_rx[i].v[1], g_rx[i].v[2], g_rx[i].v[3], g_rx[i].v[4], g_rx[i].v[5], 1);
    g_nrx = 0;
    if (g_role == 1) {
        /* host: forget clients that left; tell every client the others */
        for (i = 0; i < g_nep;)
            if (t - g_ep[i].seen > EP_TIMEOUT) {
                g_ep[i] = g_ep[--g_nep];
                g_nextPeers = 0;
            } else
                i++;
        if (t >= g_nextPeers && g_sock != INVALID_SOCKET) {
            int k;
            g_nextPeers = t + 2.0;
            for (i = 0; i < g_nep; i++) {
                char buf[MAGIC_LEN + 3 + 6 * MAXEP];
                int len = header(buf, PK_PEERS), m = 0;
                for (k = 0; k < g_nep; k++)
                    if (k != i) {
                        memcpy(buf + len + 1 + 6 * m, &g_ep[k].ip, 4);
                        memcpy(buf + len + 1 + 6 * m + 4, &g_ep[k].port, 2);
                        m++;
                    }
                buf[len] = (char)m;
                send_to(&g_ep[i], buf, len + 1 + 6 * m);
            }
        }
    } else if (g_role == 2 && t >= g_nextHello && g_sock != INVALID_SOCKET) {
        /* client: announce ourselves to the host and keep the paths to the other clients open */
        char buf[MAGIC_LEN + 2];
        g_nextHello = t + HELLO_EVERY;
        header(buf, PK_HELLO);
        send_to(&g_host, buf, sizeof(buf));
        for (i = 0; i < g_nep; i++)
            send_to(&g_ep[i], buf, sizeof(buf));
    }
    if (g_role != 1 && g_role != 2)
        g_nep = 0;
    LeaveCriticalSection(&g_lock);
}

void direct_stats(unsigned long *tx, unsigned long *rx, unsigned long *peers)
{
    *tx = g_txBanks;
    *rx = g_rxBanks;
    *peers = (unsigned long)g_nep;
}
