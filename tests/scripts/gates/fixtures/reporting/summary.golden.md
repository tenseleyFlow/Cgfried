## Perf summary — x86_64-linux-gnu (ci)
| metric | baseline | current | delta | gate |
|---|---:|---:|---:|---|
| self.wall_ms_median | 100 | 130 | +30.0% | N/A (n/a: shared CI never; fleet +30%) |
| self.user+sys_ms_median | 100 | 130 | +30.0% | N/A (n/a: shared CI never; fleet +30%) |
| self.maxrss_kb_max | 1000 | 1200 | +20.0% | PASS (+20%) |
| cgf.size_stripped | 1000 | 1150 | +15.0% | PASS (+15%) |
| hello-O2.size | 100 | 115 | +15.0% | PASS (+15%) |
| cgf.size_unstripped | 1500 | 1800 | +20.0% | REPORT (report-only) |
| matmul-64.icount | 100 | 102 | +2.0% | PASS (max(+2%, +2 instr)) |
| matmul-64.text | 100 | 105 | +5.0% | PASS (+5%) |
| self.stat.arena.ast.peak_kb | 500 | 525 | +5.0% | REPORT (report-only) |
| self.stat.intern.hit_pct | 90 | 91 | +1.1% | REPORT (report-only) |
| self.stat.intern.lookups | n/a | 1000 | n/a | REPORT (report-only) |
