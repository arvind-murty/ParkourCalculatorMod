# Provisional endpoints of the first five accepted windows

Commit1 passes. These are the final positions of each independently optimized lookahead window, not the stitched route’s later committed positions. Tick indices are zero-based. Both reproductions matched all saved committed-prefix X/Z positions within2.3e-12 blocks and yaws within5e-10degrees.

## Minecraft replay endpoints

| Window jumps | End tick | Normal X | Normal Z | Fixed X | Fixed Z | Fixed − normal Z |
|---|---:|---:|---:|---:|---:|---:|
| 1–7 | 68 | 729.825880490 | -1092.870301231 | 729.773014058 | -1092.540865306 | +0.329435925 |
| 2–8 | 71 | 730.062994410 | -1091.974268695 | 729.804995649 | -1091.542683049 | +0.431585646 |
| 3–9 | 75 | 731.324016616 | -1090.351205654 | 731.300639732 | -1090.262903758 | +0.088301896 |
| 4–10 | 77 | 731.901613445 | -1089.524041955 | 731.863792227 | -1089.423106808 | +0.100935148 |
| 5–11 | 80 | 732.898974202 | -1088.513111348 | 732.868348643 | -1088.404596785 | +0.108514563 |

## Continuous model endpoints

| Window jumps | Normal X | Normal Z | Fixed X | Fixed Z | Fixed − normal Z |
|---|---:|---:|---:|---:|---:|
| 1–7 | 729.825448372 | -1092.871062806 | 729.772627788 | -1092.541634271 | +0.329428535 |
| 2–8 | 730.062645592 | -1091.974941444 | 729.804543356 | -1091.543295954 | +0.431645490 |
| 3–9 | 731.323730270 | -1090.351900045 | 731.300300012 | -1090.263373012 | +0.088527033 |
| 4–10 | 731.901460921 | -1089.524331034 | 731.863582523 | -1089.423439822 | +0.100891212 |
| 5–11 | 732.898872038 | -1088.513243981 | 732.868265522 | -1088.404705626 | +0.108538355 |

Every fixed endpoint is further in positive Z, but the committed state after jump5 is behind normal by0.029157394 in Z. Optimizing the lookahead endpoint better does not imply better position or continuation at an earlier committed boundary. This compares different margins, entry states, retained endpoint walls, dual caps and smoothing; no single-change causal claim follows.

[Normal data](normal-endpoints.csv), [fixed data](fixed-endpoints.csv), [reproduction settings](metadata.txt). Only the two focused five-window probes ran.
