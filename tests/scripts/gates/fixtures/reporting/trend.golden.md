# Performance trend — last 90 days

| target / host | metric | first | latest | delta | threshold | trend | status |
|---|---|---:|---:|---:|---|---|---|
| x86_64-linux-gnu / kasumi | matmul-64.cgf.wall_ms_median | 100 | 111 | +11.0% | +10% (runtime; per-run gate also requires >4 MAD) | `._^` | FLAG |
| x86_64-linux-gnu / kasumi | matmul-64.gcc.wall_ms_median | 50 | 75 | +50.0% | report-only (gcc runtime reference) | `._^` | OK |
| x86_64-linux-gnu / kasumi | self.wall_ms_median | 100 | 131 | +31.0% | +30% | `._^` | FLAG |
| x86_64-linux-gnu / kasumi | sieve.cgf.wall_ms_median | 100 | 110 | +10.0% | +10% (runtime; per-run gate also requires >4 MAD) | `.-^` | OK |
| x86_64-linux-gnu / kasumi | self.user+sys_ms_median | 100 | 131 | +31.0% | +30% | `._^` | FLAG |
| x86_64-linux-gnu / kasumi | self.maxrss_kb_max | 1000 | 1210 | +21.0% | +20% | `._^` | FLAG |
| x86_64-linux-gnu / kasumi | cgf.size_stripped | 100 | 115 | +15.0% | +15% | `.-^` | OK |
| x86_64-linux-gnu / kasumi | cgf.size_unstripped | 140 | 200 | +42.9% | report-only (unstripped size) | `._^` | OK |
| x86_64-linux-gnu / kasumi | matmul-64.icount | 100 | 103 | +3.0% | max(+2%, +2 instr) | `._^` | FLAG |
| x86_64-linux-gnu / kasumi | cgf.text | 100 | 120 | +20.0% | report-only (size section) | `.-^` | OK |
| x86_64-linux-gnu / kasumi | hello.O2.text | 100 | 120 | +20.0% | report-only (size section) | `.-^` | OK |
| x86_64-linux-gnu / kasumi | hello.Os.text | 100 | 115 | +15.0% | report-only (size section) | `._^` | OK |
| x86_64-linux-gnu / kasumi | matmul-64.text | 100 | 106 | +6.0% | +5% | `.-^` | FLAG |
| x86_64-linux-gnu / kasumi | self.stat.intern.hit_pct | 90 | 92 | +2.2% | report-only | `.-^` | OK |
