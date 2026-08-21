# Scoring and Ranking Calculation (Cache V2)

This document describes how `wlauncher` computes fuzzy match scores and usage-based ranking.

## Overview

`wlauncher` combines two signals:

1. **Text relevance** (name/exec fuzzy matching)
2. **Usage history** (launch behavior over time)

The final result is sorted by a numeric score where **lower is better**.

---

## Constants

Defined in `types.h`:

- `SCORE_APPSTART_CLAMP`  
  Maximum allowed usage score per app.
- `SCORE_APPSTART_UP`  
  Added to selected app when it is launched.
- `SCORE_APPSTART_DOWN`  
  Multiplicative decay applied to all apps on launch.
- `SCORE_APPSTART_BONI`  
  Initial usage score for newly discovered apps.
- `SCORE_APPSTART_WEIGHT`  
  Converts usage score into ranking impact.
- `SCORE_APPSTART_EPSILON`  
  Tiny cutoff, values below become `0.0`.

Fuzzy constants:

- `SCORE_FUZZY_BASE`
- `SCORE_FUZZY_MATCH`
- `SCORE_FUZZY_MALI`
- `SCORE_FUZZY_MALI_CMD`

---

## App Discovery / Initialization

When desktop entries are scanned:

- New app gets:

```c
app->usage_score = SCORE_APPSTART_BONI;
```

This provides a small cold-start visibility boost for new apps.

---

## Fuzzy Match Score

For each app and search query:

- Compute score against `name`
- Compute score against `exec`
- No match is represented as `-1`
- Lower score means better textual match

Matching policy:

- `name` match is preferred over `exec`
- `exec` match receives an additional penalty (`SCORE_FUZZY_MALI_CMD` or stronger)

---

## Final Ranking Score

After base fuzzy score is determined, usage is applied:

```c
final_score = base_score - (int)(usage_score * SCORE_APPSTART_WEIGHT);
if (final_score < 0) final_score = 0;
```

Interpretation:

- Better text match => lower `base_score`
- Higher usage => larger subtraction => better rank
- Clamp to avoid negative values

All candidates are sorted ascending by `final_score`, then truncated to `MAX_MATCHED_APPS`.

---

## Usage Update on Launch (Enter)

When user executes a selection:

1. Apply decay to all apps:

```c
usage_score *= SCORE_APPSTART_DOWN;
if (usage_score < SCORE_APPSTART_EPSILON) usage_score = 0.0;
```

2. Boost selected app:

```c
usage_score += SCORE_APPSTART_UP;
if (usage_score > SCORE_APPSTART_CLAMP) usage_score = SCORE_APPSTART_CLAMP;
```

This gives:

- Fast reinforcement for frequently used apps
- Automatic fading of stale history
- No timestamp bookkeeping needed

---

## Empty Query Behavior

For empty search input, ranking can still use usage scores so frequent apps appear first.

Typical strategy:

- Compute a usage-derived score for all apps
- Sort ascending
- Show top `MAX_MATCHED_APPS`

---

## Why This Model Works

- **Simple**: few parameters, no complex history structures
- **Adaptive**: responds quickly to behavior changes
- **Stable**: clamp and epsilon prevent runaway/float noise
- **Practical**: new apps are discoverable, unused ones naturally fade

---

## Tuning Guide

If usage dominates too much:

- Decrease `SCORE_APPSTART_WEIGHT`
- Decrease `SCORE_APPSTART_UP`
- Increase `SCORE_APPSTART_DOWN` slightly (e.g. `0.95 -> 0.97`)

If usage influence is too weak:

- Increase `SCORE_APPSTART_WEIGHT`
- Increase `SCORE_APPSTART_UP`
- Decrease `SCORE_APPSTART_DOWN` slightly (e.g. `0.97 -> 0.95`)

If new apps appear too high:

- Decrease `SCORE_APPSTART_BONI`

If new apps are never discovered:

- Increase `SCORE_APPSTART_BONI` slightly
