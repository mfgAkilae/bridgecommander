/* Orientation dead reckoning for remote ships (OrientationCorrection=2).
 *
 * State updates carry the sender's forward and up vectors (NetworkAI +0x3C/+0x48)
 * and its game time (+0x2C). The vectors are one latency old on arrival, and the
 * sender only includes them when they changed. From two received forward vectors and
 * their sender times we estimate the turn rate (axis f_prev x f_new, angle atan2) and
 * rotate the received orientation by rate * (latency compensation + time since the
 * update arrived). netfix.c turns the proxy towards that predicted orientation. */
#include "common.h"

double __cdecl sqrt(double);
double __cdecl atan2(double, double);
double __cdecl sin(double);
double __cdecl cos(double);

#define ADDR_APP_PTR 0x009A09D0 /* app object; game time at +0x90 */
#define MAX_RATE 3.0f           /* rad/s, cap of the estimated turn rate */
#define MAX_HORIZON 0.5f        /* s, cap of the prediction */
#define STALE_S 0.3f            /* no forward change for this long: rate decays to 0 */

#define MAXSHIPS 64
static struct {
    BYTE *ai;
    float f[3], u[3]; /* last received forward / up */
    float sentAt;     /* sender game time of the last forward change */
    float gotAt;      /* local game time when the last update arrived */
    float w[3];       /* turn rate vector (axis * rad/s) */
    float lead;       /* latency compensation of that update, s */
    float p0[3];      /* received position (before latency compensation) */
    int valid, hasPos;
} g_ship[MAXSHIPS];

static float game_time(void)
{
    BYTE *app = *(BYTE **)ADDR_APP_PTR;
    return app ? *(float *)(app + 0x90) : 0.0f;
}

static int slot_of(BYTE *ai, int create)
{
    int i, freeSlot = -1;
    for (i = 0; i < MAXSHIPS; i++) {
        if (g_ship[i].ai == ai)
            return i;
        if (freeSlot < 0 && !g_ship[i].ai)
            freeSlot = i;
    }
    if (!create)
        return -1;
    if (freeSlot < 0)
        freeSlot = (int)(((DWORD)ai >> 4) % MAXSHIPS);
    memset(&g_ship[freeSlot], 0, sizeof(g_ship[0]));
    g_ship[freeSlot].ai = ai;
    return freeSlot;
}

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* rotate v around the rotation vector w*t (Rodrigues) */
static void rotate(const float *v, const float *w, float t, float *out)
{
    float k[3], phi, n = (float)sqrt(dot3(w, w)), c, s, kv[3], kd;
    int i;
    if (n < 1e-6f || t <= 0.0f) {
        for (i = 0; i < 3; i++)
            out[i] = v[i];
        return;
    }
    for (i = 0; i < 3; i++)
        k[i] = w[i] / n;
    phi = n * t;
    c = (float)cos(phi);
    s = (float)sin(phi);
    kv[0] = k[1] * v[2] - k[2] * v[1];
    kv[1] = k[2] * v[0] - k[0] * v[2];
    kv[2] = k[0] * v[1] - k[1] * v[0];
    kd = dot3(k, v);
    for (i = 0; i < 3; i++)
        out[i] = v[i] * c + kv[i] * s + k[i] * kd * (1.0f - c);
}

/* called by the state update reader hook after every update of this proxy that
 * carried a position (p0: that position, before latency compensation) */
void orient_on_update(BYTE *ai, int leadMs, const float *p0)
{
    int i = slot_of(ai, 1);
    float *f = (float *)(ai + 0x3C), *u = (float *)(ai + 0x48);
    float ts = *(float *)(ai + 0x2C);
    if (dot3(f, f) < 0.25f || dot3(u, u) < 0.25f)
        return;
    g_ship[i].gotAt = game_time();
    g_ship[i].lead = leadMs > 0 ? leadMs / 1000.0f : 0.0f;
    if (p0) {
        memcpy(g_ship[i].p0, p0, 12);
        g_ship[i].hasPos = 1;
    }
    if (!g_ship[i].valid) {
        memcpy(g_ship[i].f, f, 12);
        memcpy(g_ship[i].u, u, 12);
        g_ship[i].sentAt = ts;
        g_ship[i].valid = 1;
        return;
    }
    if (memcmp(g_ship[i].f, f, 12) != 0) {
        float dt = ts - g_ship[i].sentAt;
        if (dt > 0.005f && dt < 1.0f) {
            float *p = g_ship[i].f, ax[3], sn, cs, ang, rate, wn[3];
            int k;
            ax[0] = p[1] * f[2] - p[2] * f[1];
            ax[1] = p[2] * f[0] - p[0] * f[2];
            ax[2] = p[0] * f[1] - p[1] * f[0];
            sn = (float)sqrt(dot3(ax, ax));
            cs = dot3(p, f);
            ang = (float)atan2(sn, cs);
            rate = ang / dt;
            if (rate > MAX_RATE)
                rate = MAX_RATE;
            for (k = 0; k < 3; k++)
                wn[k] = sn > 1e-6f ? ax[k] / sn * rate : 0.0f;
            for (k = 0; k < 3; k++) /* smooth: quantised vectors make single steps noisy */
                g_ship[i].w[k] = 0.5f * g_ship[i].w[k] + 0.5f * wn[k];
        } else {
            memset(g_ship[i].w, 0, sizeof(g_ship[i].w));
        }
        memcpy(g_ship[i].f, f, 12);
        g_ship[i].sentAt = ts;
    } else if (ts - g_ship[i].sentAt > STALE_S) {
        memset(g_ship[i].w, 0, sizeof(g_ship[i].w)); /* not turning (any more) */
    }
    memcpy(g_ship[i].u, u, 12);
    if (trace_active())
        tracef("W ai=%p w=%.3f %.3f %.3f |w|=%.3f lead=%.3f", ai, g_ship[i].w[0], g_ship[i].w[1], g_ship[i].w[2],
               (float)sqrt(dot3(g_ship[i].w, g_ship[i].w)), g_ship[i].lead);
}

/* predicted forward/up vectors of this proxy for now; 0 if none */
int orient_predict(BYTE *ai, float *f, float *u)
{
    int i = slot_of(ai, 0);
    float h;
    if (i < 0 || !g_ship[i].valid)
        return 0;
    h = g_ship[i].lead + (game_time() - g_ship[i].gotAt);
    if (h < 0.0f)
        h = 0.0f;
    if (h > MAX_HORIZON)
        h = MAX_HORIZON;
    rotate(g_ship[i].f, g_ship[i].w, h, f);
    rotate(g_ship[i].u, g_ship[i].w, h, u);
    return 1;
}

/* Arc extrapolation of the position (ArcExtrapolation=1, with OrientationCorrection=2):
 * the received position moved along the predicted turn instead of a straight line,
 * p(h) = p0 + speed * integral_0^h R(w t) f dt, h = latency compensation + age of the
 * update. Closed form with unit axis k, rate r, phi = r h, f_par = k (k.f),
 * f_perp = f - f_par:  f_par h + sin(phi)/r f_perp + (1 - cos(phi))/r (k x f_perp).
 * Beyond MAX_HORIZON the ship continues straight in the direction reached there. */
static void arc_offset(const float *f, const float *w, float h, float *out)
{
    float r = (float)sqrt(dot3(w, w)), k[3], kd, fpar[3], fperp[3], kxf[3], s, c;
    int i;
    if (r < 1e-4f || h <= 0.0f) {
        for (i = 0; i < 3; i++)
            out[i] = f[i] * (h > 0.0f ? h : 0.0f);
        return;
    }
    for (i = 0; i < 3; i++)
        k[i] = w[i] / r;
    kd = dot3(k, f);
    for (i = 0; i < 3; i++) {
        fpar[i] = k[i] * kd;
        fperp[i] = f[i] - fpar[i];
    }
    kxf[0] = k[1] * fperp[2] - k[2] * fperp[1];
    kxf[1] = k[2] * fperp[0] - k[0] * fperp[2];
    kxf[2] = k[0] * fperp[1] - k[1] * fperp[0];
    s = (float)sin(r * h);
    c = (float)cos(r * h);
    for (i = 0; i < 3; i++)
        out[i] = fpar[i] * h + fperp[i] * (s / r) + kxf[i] * ((1.0f - c) / r);
}

int orient_target(BYTE *ai, float *out)
{
    int i = slot_of(ai, 0), k;
    float h, hc, off[3], fe[3], speed;
    if (i < 0 || !g_ship[i].valid || !g_ship[i].hasPos)
        return 0;
    speed = *(float *)(ai + 0x54);
    h = g_ship[i].lead + (game_time() - g_ship[i].gotAt);
    if (h < 0.0f)
        h = 0.0f;
    hc = h > MAX_HORIZON ? MAX_HORIZON : h;
    arc_offset(g_ship[i].f, g_ship[i].w, hc, off);
    if (h > hc) {
        rotate(g_ship[i].f, g_ship[i].w, hc, fe);
        for (k = 0; k < 3; k++)
            off[k] += fe[k] * (h - hc);
    }
    for (k = 0; k < 3; k++)
        out[k] = g_ship[i].p0[k] + off[k] * speed;
    return 1;
}
