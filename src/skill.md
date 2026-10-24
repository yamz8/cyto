---
name: cyto
description: |
  Gate flow cytometry data with Cyto. Use for ANY request to inspect FCS files,
  set up or change a gating strategy, apply gates across samples, compensate
  with a spillover matrix, or produce population counts, percentages, and
  medians from .fcs files or a .cyto workspace.
---

# Cyto

A Cyto workspace is one `.cyto` JSON file that lists FCS files and holds one
gate tree, applied the same way to every sample. You write it with your
ordinary editing tools; the `cyto` command inspects FCS files, checks the
workspace, and computes the statistics. No command needs a display. The FCS
files are only ever read.

## Commands

- `cyto info sample.fcs --json`: events, every channel's name (`$PnN`), stain
  label (`$PnS`), suggested scale, and its 1st percentile, median, and 99th
  percentile, plus the channels of any spillover matrix in the file. Run it
  before writing gates, to see the channel names and where the values sit.
- `cyto check analysis.cyto --json`: every problem with the JSON, the gate
  tree, the samples, and the compensation, plus the population paths. Fix all
  of them.
- `cyto stats analysis.cyto --json`: one row per sample and population with
  `events`, `percentOfParent`, `percentOfTotal`, and the median of every
  channel. Without `--json` it prints CSV; `-o stats.csv` writes the file.
  Name FCS files after the workspace (`cyto stats analysis.cyto new/*.fcs`) to
  apply the same gates to another batch instead of the workspace's samples.

## Format

```json
{
  "format": "cyto",
  "version": 1,
  "samples": ["donor-01.fcs", "donor-02.fcs"],
  "compensation": {
    "enabled": true,
    "channels": ["FL1-A", "FL2-A"],
    "matrix": [[1, 0.18], [0.02, 1]]
  },
  "scales": { "FL1-A": "arcsinh" },
  "gates": [
    { "id": 1, "parent": 0, "name": "Lymphocytes", "type": "rectangle",
      "x": "FSC-A", "y": "SSC-A", "xScale": "linear", "yScale": "linear",
      "points": [[35000, 6000], [70000, 32000]] },
    { "id": 2, "parent": 1, "name": "CD3+", "type": "range",
      "x": "FL1-A", "xScale": "arcsinh", "points": [3.3, 9] },
    { "id": 3, "parent": 2, "name": "CD4+", "type": "polygon",
      "x": "FL2-A", "y": "FL3-A", "xScale": "arcsinh", "yScale": "arcsinh",
      "points": [[3.7, -2], [9, -2], [9, 2.6], [3.7, 2.6]] }
  ]
}
```

- `samples` are paths relative to the workspace file.
- Population 0 is all events. Every gate is a population inside its `parent`
  (0 or another gate's `id`); ids are unique positive integers. `name` is what
  the statistics call it; the population's path joins the names with `/`.
- `type` is `rectangle` or `ellipse` (two opposite corners of the box),
  `polygon` (three or more vertices), or `range` (two numbers, the bounds on
  `x`, for a histogram gate; it has no `y`).
- `x` and `y` are channel names exactly as `cyto info` lists them.
- **Points are in scaled coordinates**, on the gate's own `xScale` and
  `yScale`: `linear` is the value itself, `log` is log10 of the value (values
  below 1 count as 1), and `arcsinh` is asinh(value / 150). So 10,000 on an
  arcsinh axis is asinh(10000 / 150) ≈ 4.89, and 0 is 0. Use arcsinh for
  fluorescence, where compensated values go below zero, and linear for scatter.
- To take in everything beyond a bound, use a far value (a corner at 1e9 on a
  linear axis, or 30 on arcsinh) rather than the plot's edge.
- `compensation.matrix` is the spillover matrix, one row per channel in
  `channels`: row i is how much of channel i's dye each detector sees, so the
  diagonal is 1. Gates apply to compensated values when `enabled` is true.
  FCS files usually carry their own in `$SPILLOVER`; `cyto info` shows which
  channels it covers.
- `scales` only sets how the window draws each channel; leave it out if unsure.
- The window also saves `views` and `population` (which axes it showed where);
  keep them as they are when editing a saved workspace.

## Workflow

1. `cyto info` each kind of file to learn the channels and where the
   populations sit (medians and percentiles).
2. Write the workspace: samples, the file's spillover matrix as
   `compensation`, and the gates from the outside in (scatter, then markers).
3. `cyto check analysis.cyto --json` until `valid` is true.
4. `cyto stats analysis.cyto --json` and read the numbers back. A population
   that is empty or holds everything usually means a bound on the wrong scale.
5. Tell the person they can open the workspace in the window with
   `cyto open analysis.cyto` to look at the plots and adjust the gates by hand.
