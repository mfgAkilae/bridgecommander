/* Velocity-based extrapolation of remote ships (VelocityExtrapolation, issue #11).
 *
 * NetworkAI extrapolates the target along the reported nose * speed. In turns BC ships
 * drift: their velocity lags the nose (median 6-23 degrees, 90th percentile up to 60
 * degrees, see docs/measurements.md), so that line points the wrong way. Here the
 * velocity is estimated from successive received positions and their sender times,
 * and the target is p0 + v * h (h = latency compensation + age of the update).
 * Mode 2 also estimates how fast the velocity turns and extrapolates along that arc.
 * The proxy itself is still flown by NetworkAI along its nose, i.e. with the drift
 * of its own ship class. */
#include "common.h"

double __cdecl sqrt(double);
double __cdecl atan2(double, double);
double __cdecl sin(double);
double __cdecl cos(double);

#define ADDR_APP_PTR 0x009A09D0 /* app object; game time at +0x90 */
#define MAX_GAP_S 1.0f          /* longer without a position: start over */
#define MAX_RATE 3.0f           /* rad/s */
#define MAX_HORIZON 0.5f        /* s, curved part of the extrapolation */

#define MAXSHIPS 64
static struct {
    BYTE *ai;
    float p[3];  /* last received position (before latency compensation) */
    float ts;    /* its sender time */
    float gotAt; /* local game time when it arrived */
    float lead;  /* latency compensation, s */
    float v[3];  /* estimated velocity */
    float w[3];  /* turn rate of the velocity (axis * rad/s) */
    int n;       /* number of velocity estimates since the last reset */
} g_ship[MAXSHIPS];

static float game_time(void)
{
    BYTE *app = *(BYTE **)ADDR_APP_PTR;
    return app ? *(float *)(app + 0x90) : 0.0f;
}

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
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

/* called by the reader hook for every update that carried a position */
void velo_on_update(BYTE *ai, int leadMs, const float *p0)
{
    int i = slot_of(ai, 1), k;
    float ts = *(float *)(ai + 0x2C), dt = ts - g_ship[i].ts, vr[3], speed = *(float *)(ai + 0x54);
    float lim = 3.0f * (speed < 0 ? -speed : speed) + 20.0f;
    g_ship[i].gotAt = game_time();
    g_ship[i].lead = leadMs > 0 ? leadMs / 1000.0f : 0.0f;
    if (g_ship[i].ts > 0.0f && dt > 0.005f && dt < MAX_GAP_S) {
        for (k = 0; k < 3; k++)
            vr[k] = (p0[k] - g_ship[i].p[k]) / dt;
        if (dot3(vr, vr) < lim * lim) {
            if (g_ship[i].n == 0) {
                memcpy(g_ship[i].v, vr, 12);
            } else {
                float vo[3], ax[3], sn, cs, rate, no, nn;
                memcpy(vo, g_ship[i].v, 12);
                for (k = 0; k < 3; k++) /* smooth: positions are exact, sender times are not */
                    g_ship[i].v[k] = 0.5f * g_ship[i].v[k] + 0.5f * vr[k];
                /* turn rate of the velocity vector */
                no = (float)sqrt(dot3(vo, vo));
                nn = (float)sqrt(dot3(g_ship[i].v, g_ship[i].v));
                if (no > 1.0f && nn > 1.0f) {
                    float *vn = g_ship[i].v;
                    ax[0] = vo[1] * vn[2] - vo[2] * vn[1];
                    ax[1] = vo[2] * vn[0] - vo[0] * vn[2];
                    ax[2] = vo[0] * vn[1] - vo[1] * vn[0];
                    sn = (float)sqrt(dot3(ax, ax)) / (no * nn);
                    cs = dot3(vo, vn) / (no * nn);
                    rate = (float)atan2(sn, cs) / dt;
                    if (rate > MAX_RATE)
                        rate = MAX_RATE;
                    for (k = 0; k < 3; k++)
                        g_ship[i].w[k] = 0.5f * g_ship[i].w[k] +
                                         0.5f * (sn > 1e-6f ? ax[k] / (sn * no * nn) * rate : 0.0f);
                } else {
                    memset(g_ship[i].w, 0, sizeof(g_ship[i].w));
                }
            }
            g_ship[i].n++;
        } else {
            g_ship[i].n = 0; /* jump (respawn, teleport): start over */
            memset(g_ship[i].w, 0, sizeof(g_ship[i].w));
        }
    } else {
        g_ship[i].n = 0;
        memset(g_ship[i].w, 0, sizeof(g_ship[i].w));
    }
    memcpy(g_ship[i].p, p0, 12);
    g_ship[i].ts = ts;
    if (trace_active())
        tracef("V ai=%p v=%.2f %.2f %.2f |v|=%.2f spd=%.2f n=%d |w|=%.3f", ai, g_ship[i].v[0], g_ship[i].v[1],
               g_ship[i].v[2], (float)sqrt(dot3(g_ship[i].v, g_ship[i].v)), speed, g_ship[i].n,
               (float)sqrt(dot3(g_ship[i].w, g_ship[i].w)));
}

/* rotate v around w*t (Rodrigues) */
static void rotate(const float *v, const float *w, float t, float *out)
{
    float k[3], n = (float)sqrt(dot3(w, w)), c, s, kv[3], kd;
    int i;
    if (n < 1e-6f || t <= 0.0f) {
        memcpy(out, v, 12);
        return;
    }
    for (i = 0; i < 3; i++)
        k[i] = w[i] / n;
    c = (float)cos(n * t);
    s = (float)sin(n * t);
    kv[0] = k[1] * v[2] - k[2] * v[1];
    kv[1] = k[2] * v[0] - k[0] * v[2];
    kv[2] = k[0] * v[1] - k[1] * v[0];
    kd = dot3(k, v);
    for (i = 0; i < 3; i++)
        out[i] = v[i] * c + kv[i] * s + k[i] * kd * (1.0f - c);
}

/* integral_0^h R(w t) v dt (see orient.c) */
static void arc(const float *v, const float *w, float h, float *out)
{
    float r = (float)sqrt(dot3(w, w)), k[3], kd, par[3], perp[3], kx[3], s, c;
    int i;
    if (r < 1e-4f) {
        for (i = 0; i < 3; i++)
            out[i] = v[i] * h;
        return;
    }
    for (i = 0; i < 3; i++)
        k[i] = w[i] / r;
    kd = dot3(k, v);
    for (i = 0; i < 3; i++) {
        par[i] = k[i] * kd;
        perp[i] = v[i] - par[i];
    }
    kx[0] = k[1] * perp[2] - k[2] * perp[1];
    kx[1] = k[2] * perp[0] - k[0] * perp[2];
    kx[2] = k[0] * perp[1] - k[1] * perp[0];
    s = (float)sin(r * h);
    c = (float)cos(r * h);
    for (i = 0; i < 3; i++)
        out[i] = par[i] * h + perp[i] * (s / r) + kx[i] * ((1.0f - c) / r);
}

/* Extrapolated target and direction of flight now; 0 if no velocity estimate yet. */
int velo_target(BYTE *ai, float *target, float *dir)
{
    int i = slot_of(ai, 0), k;
    float h, hc, off[3], ve[3], vu[3], n;
    if (i < 0 || g_ship[i].n < 1)
        return 0;
    h = g_ship[i].lead + (game_time() - g_ship[i].gotAt);
    if (h < 0.0f)
        h = 0.0f;
    /* direction from the received positions (it shows the drift), magnitude from the
     * reported speed: the sender computes that from its exact velocity, while position
     * differences over 50 ms scatter by about +-30 % (sampling of position vs. time) */
    {
        float sp = *(float *)(ai + 0x54), nv = (float)sqrt(dot3(g_ship[i].v, g_ship[i].v));
        if (sp < 0.0f)
            sp = -sp;
        if (nv > 0.5f)
            for (k = 0; k < 3; k++)
                vu[k] = g_ship[i].v[k] / nv * sp;
        else
            memcpy(vu, g_ship[i].v, 12);
    }
    if (g_cfg.velocityExtrapolation == 2) {
        hc = h > MAX_HORIZON ? MAX_HORIZON : h;
        arc(vu, g_ship[i].w, hc, off);
        rotate(vu, g_ship[i].w, hc, ve);
        for (k = 0; k < 3; k++)
            off[k] += ve[k] * (h - hc);
    } else {
        memcpy(ve, vu, 12);
        for (k = 0; k < 3; k++)
            off[k] = ve[k] * h;
    }
    for (k = 0; k < 3; k++)
        target[k] = g_ship[i].p[k] + off[k];
    n = (float)sqrt(dot3(ve, ve));
    if (n > 1e-3f)
        for (k = 0; k < 3; k++)
            dir[k] = ve[k] / n;
    else
        memset(dir, 0, 12);
    return 1;
}
