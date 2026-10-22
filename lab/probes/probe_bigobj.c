/* Stress probe for a large OBJ (owner fixture: assets/utah_teapot_test_2mil_tri.obj,
   ~2.26M triangles after quad triangulation). Measures what the product's own code costs
   on that model: parse time, buffer sizes, peak RSS, pick_triangle cost per cursor, and the
   free path. No GL here — obj.c and camera.c are pure CPU, so the numbers describe the
   parser and the picker, not the GPU. PASS/FAIL thresholds are relative budgets so the probe
   survives a different machine (see PROJECT_MEMORY.md §6). */
#include "obj.h"
#include "camera.h"
#include "vmath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <psapi.h>

static double qpc_ms(void) {
  static LARGE_INTEGER freq;   /* zero-initialised, filled on the first call */
  LARGE_INTEGER n;
  if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&n);
  return (double)n.QuadPart * 1000.0 / (double)freq.QuadPart;
}

static size_t rss_bytes(void) {
  PROCESS_MEMORY_COUNTERS pmc;
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) return 0;
  return (size_t)pmc.WorkingSetSize;
}

static size_t peak_rss_bytes(void) {
  PROCESS_MEMORY_COUNTERS pmc;
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) return 0;
  return (size_t)pmc.PeakWorkingSetSize;
}

/* Deterministic cursor samples spread over the middle of the frame: a full-screen sweep
   would mostly hit empty background and measure a cheaper (cheaper != useful) scan. */
static unsigned long lcg(unsigned long* s) { *s = *s * 1103515245UL + 12345UL; return (*s >> 8) & 0xffff; }

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "assets/utah_teapot_test_2mil_tri.obj";
  const int W = 800, H = 600;
  const int samples = 60;   /* each ray scans every triangle, so keep the count modest */
  int fails = 0;

  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "FAIL cannot read fixture '%s'\n", path); return 1; }
  fseek(f, 0, SEEK_END);
  long file_bytes = ftell(f);
  fclose(f);

  obj_mesh m;
  memset(&m, 0, sizeof m);
  char err[256];
  double t0 = qpc_ms();
  double rss_before = (double)rss_bytes();
  if (obj_parse_file(path, &m, err, sizeof err) < 0) {
    fprintf(stderr, "FAIL parse: %s\n", err);
    return 1;
  }
  double parse_ms = qpc_ms() - t0;
  double rss_after = (double)rss_bytes();

  size_t v_bytes = (size_t)m.vcount * 8 * sizeof(float);
  size_t fid_bytes = (size_t)m.triangles * sizeof(int);
  size_t vid_bytes = (size_t)m.vcount * sizeof(int);

  printf("fixture %s: %ld bytes (%.1f MB)\n", path, file_bytes, file_bytes / (1024.0 * 1024.0));
  printf("parse: %.0f ms -> faces %d, triangles %d, vertices %d, flags %d\n",
         parse_ms, m.faces, m.triangles, m.vcount, m.flags);
  printf("buffers: v %.1f MB (%d x 8 floats), face_ids %.1f MB, vert_ids %.1f MB, total %.1f MB\n",
         v_bytes / (1024.0 * 1024.0), m.vcount, fid_bytes / (1024.0 * 1024.0),
         vid_bytes / (1024.0 * 1024.0), (v_bytes + fid_bytes + vid_bytes) / (1024.0 * 1024.0));
  printf("rss: before %.1f MB, after parse %.1f MB (+%.1f MB), peak %.1f MB\n",
         rss_before / (1024.0 * 1024.0), rss_after / (1024.0 * 1024.0),
         (rss_after - rss_before) / (1024.0 * 1024.0), peak_rss_bytes() / (1024.0 * 1024.0));

  /* Sanity: the fixture must be the big one, otherwise the numbers mean nothing. */
  if (m.triangles < 1000000) {
    printf("FAIL triangles %d < 1e6 - this is not the stress fixture\n", m.triangles);
    fails++;
  }
  if (m.vcount != m.triangles * 3) {
    printf("FAIL vcount %d != triangles*3 %d\n", m.vcount, m.triangles * 3);
    fails++;
  }
  if (!(m.flags & OBJ_NORMALS_AUTHORED)) {
    printf("FAIL fixture carries no authored vn (flags %d)\n", m.flags);
    fails++;
  }

  camera cam;
  camera_reset(&cam, m.radius);
  camera_set_aspect(&cam, (float)W / (float)H);
  float aspect = (float)W / (float)H;

  unsigned long s = 12345;
  double total_ms = 0.0, worst_ms = 0.0;
  int hits = 0, misses = 0;
  int first_hit = -1;

  /* Cursor rays through the middle band of the frame, where the teapot body sits. */
  for (int i = 0; i < samples; i++) {
    double mx = W * (0.25 + 0.5 * (lcg(&s) / 65536.0));
    double my = H * (0.25 + 0.5 * (lcg(&s) / 65536.0));
    float depth = 0.0f;
    double p0 = qpc_ms();
    int tri = pick_triangle(&cam, &m, mx, my, W, H, aspect, &depth);
    double dt = qpc_ms() - p0;
    total_ms += dt;
    if (dt > worst_ms) worst_ms = dt;
    if (tri >= 0) { hits++; if (first_hit < 0) first_hit = tri; }
    else misses++;
  }

  double avg_ms = samples ? total_ms / samples : 0.0;
  printf("pick: %d rays (hits %d, misses %d), total %.0f ms, avg %.1f ms, worst %.1f ms\n",
         samples, hits, misses, total_ms, avg_ms, worst_ms);
  printf("pick density: %.1f ms per 1M triangles (triangles %d)\n",
         avg_ms / (m.triangles / 1e6), m.triangles);
  printf("first hit: triangle %d, identity face %d\n", first_hit, m.face_ids ? m.face_ids[first_hit] : -1);

  if (hits == 0) { printf("FAIL pick found nothing on %d rays through the model\n", samples); fails++; }
  if (avg_ms > 200.0) { printf("FAIL avg pick %.1f ms > 200 ms budget\n", avg_ms); fails++; }
  if (worst_ms > 400.0) { printf("FAIL worst pick %.1f ms > 400 ms budget\n", worst_ms); fails++; }

  double f0 = qpc_ms();
  obj_mesh_free(&m);
  double free_ms = qpc_ms() - f0;
  printf("free: %.0f ms, rss after free %.1f MB (peak %.1f MB)\n",
         free_ms, rss_bytes() / (1024.0 * 1024.0), peak_rss_bytes() / (1024.0 * 1024.0));

  if (fails) { printf("probe_bigobj: FAIL (%d checks failed)\n", fails); return 1; }
  printf("probe_bigobj: PASS\n");
  return 0;
}
