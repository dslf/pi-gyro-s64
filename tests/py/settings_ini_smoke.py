#!/usr/bin/env python3
"""Independent check of settings.ini: format, key set, value ranges.

The C side owns the table SETTINGS_FIELDS[]; this script re-states the same contract in
Python so a serializer bug (wrong key name, missing key, value outside its range, bool
written as a number) is caught by a second, independent parser rather than by the C reader.

Usage: python tests/py/settings_ini_smoke.py [path/to/settings.ini]
Exit 0 and "settings.ini: PASS" when the file satisfies the contract.
"""
import sys

# key -> (kind, lo, hi). kind: f float, b bool (true/false), r rgb triple in [0,1].
SPEC = {
    "fov_deg":              ("f", 15.0, 90.0),
    "ortho":                ("b", 0.0, 1.0),
    "yaw_deg":              ("f", -180.0, 180.0),
    "pitch_deg":            ("f", -89.0, 89.0),
    "dist_scale":           ("f", 0.2, 8.0),
    "light_dir":            ("r", 0.0, 1.0),
    "light_int":            ("f", 0.0, 4.0),
    "amb_top":              ("f", 0.0, 4.0),
    "amb_bot":              ("f", 0.0, 4.0),
    "base_rgb":             ("r", 0.0, 1.0),
    "shine":                ("f", 1.0, 256.0),
    "specular":             ("f", 0.0, 2.0),
    "wireframe":            ("b", 0.0, 1.0),
    "bg_rgb":               ("r", 0.0, 1.0),
    "outline_rgb":          ("r", 0.0, 1.0),
    "hud_font_scale":       ("f", 0.6, 2.5),
    "hud_bg_alpha":         ("f", 0.0, 1.0),
    "hud_info_visible":     ("b", 0.0, 1.0),
    "hud_help_w":           ("f", 300.0, 1200.0),
    "hud_stats_w":          ("f", 0.0, 800.0),
    "settings_w":           ("f", 120.0, 900.0),
    "hud_settings_visible": ("b", 0.0, 1.0),
    "sky_visible":          ("b", 0.0, 1.0),
    "outline_visible":      ("b", 0.0, 1.0),
}


def parse(path):
    keys = {}
    bad = []
    with open(path, "r", encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            if "=" not in s:
                bad.append((n, "missing '=': " + s))
                continue
            k, _, v = s.partition("=")
            k, v = k.strip(), v.strip()
            if k not in SPEC:
                bad.append((n, "unknown key: " + k))
                continue
            if k in keys:
                bad.append((n, "duplicate key: " + k))
                continue
            kind, lo, hi = SPEC[k]
            try:
                if kind == "b":
                    if v not in ("true", "false"):
                        bad.append((n, "%s: bool written as %r" % (k, v)))
                        continue
                    val = [1.0 if v == "true" else 0.0]
                elif kind == "r":
                    parts = [float(x) for x in v.split(",")]
                    if len(parts) != 3:
                        bad.append((n, "%s: rgb needs 3 numbers, got %d" % (k, len(parts))))
                        continue
                    val = parts
                else:
                    val = [float(v)]
            except ValueError:
                bad.append((n, "%s: value %r is not a number" % (k, v)))
                continue
            for x in val:
                if not (lo <= x <= hi):
                    bad.append((n, "%s: value %.6f outside [%g, %g]" % (k, x, lo, hi)))
                    break
            else:
                keys[k] = val
    return keys, bad


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "build/settings_selftest.ini"
    keys, bad = parse(path)
    missing = sorted(set(SPEC) - set(keys))
    if bad:
        print("settings.ini: FAIL %d bad line(s) in %s" % (len(bad), path))
        for n, why in bad[:8]:
            print("  line %d: %s" % (n, why))
        return 1
    if missing:
        print("settings.ini: FAIL %d key(s) missing in %s: %s" % (len(missing), path, ", ".join(missing)))
        return 1
    rgb = [k for k, v in keys.items() if len(v) == 3]
    print("settings.ini: PASS %d keys parsed from %s (%d rgb triples, %d scalars/bools), "
          "all values inside their declared ranges, 0 unknown or duplicate keys"
          % (len(keys), path, len(rgb), len(keys) - len(rgb)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
