# Mini-player regression fixtures

Run on Linux from any working directory. `--source-root` defaults to the enclosing
`sonix-player` directory when installed under `sonix-player/tests`. An optional
`--reviewed-overlay` supplies only changed files; all other sources come from the
actual checkout. There are no Git or historical output-directory dependencies.

Both runners require `--lvgl-root` pointing at the matching checkout containing
LVGL headers and (for layout) `build_host/lvgl/*.o`. These objects are read-only
and hashed. Layout also requires `--font-dir` containing approved `ui-regular.ttf`
and `ui-semibold.ttf`. Results are written only under the explicit `--results`.

```sh
python3 tests/mini_player_layout_test.py --lvgl-root /path/to/host-checkout \
  --font-dir /path/to/fonts --results /path/to/results/layout --sanitize --negative
python3 tests/mini_player_policy_test.py --lvgl-root /path/to/host-checkout \
  --results /path/to/results/policy --sanitize --negative
```

Optional `--baseline --baseline-mini /absolute/prior/sx_mini.c` draws the actual
prior implementation for four Normal-mode controls. Do not use a second behavioral
model as the baseline. Whole-module layout uses inert audio/services/artwork
adapters; this is not native acceptance. ASAN covers the fixture and support
compiled by the runner; prebuilt LVGL objects remain uninstrumented.
