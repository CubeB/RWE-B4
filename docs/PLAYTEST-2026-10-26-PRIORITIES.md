# Playtest work order: importance and implementation effort

Ranked 2026-10-27 at the owner's request. Scope: the eleven reported gameplay
problems, the ten command implementation tasks, and existing download-size
issue #745. #905 remains the command tracking parent, not an additional
implementation task. All requested commands remain in scope.

Gameplay blockers and potential outcome/fairness faults take priority over
conveniences and visual polish. Within a priority band, prefer bounded work
with high player benefit. The order is a provisional triage judgment, not a
measured severity or delivery promise. Serious aircraft/network faults should
start with evidence capture early even while smaller fixes are delivered.

Effort describes the **whole issue**, including validation: Small is a
localized change; Medium is a bounded change across a few components; Large
includes substantial subsystem, protocol or authority work; Unknown means
the report is too vague to estimate. These are static estimates with limited
confidence, especially for unreproduced bugs. They do not make a ticket
`ready-for-agent` or waive its reference/reproduction requirements.

## Recommended work order

Progress from the [2026-10-27 implementation pass](PLAYTEST-2026-10-27-IMPLEMENTATION.md):
#895's commander border fault is corrected; aircraft recovery remains open.
#904's watching visibility is corrected. #899's normal-play bubble leak now
has a separate reference finding and remains the next confirmed fix. #898's
water build-order/feedback height is corrected and covered by a real placement
scenario. Other tasks remain in the order below; this is not a completed queue.

| Order | Issue | Priority | Effort | Why / next step |
| --- | --- | --- | --- | --- |
| 1 | [#895](https://github.com/CubeB/RWE-B4/issues/895) — Units leaving the map | High | Medium | Potentially loses units or permits invalid movement; establish the boundary rule and reproduce an edge case. |
| 2 | [#899](https://github.com/CubeB/RWE-B4/issues/899) — Invisible underwater extractors | High | Medium | Basic owned-unit visibility affects underwater economy; identify the model/visibility/water-pass fault. |
| 3 | [#898](https://github.com/CubeB/RWE-B4/issues/898) — Tidal generator / underwater build-grid placement | High | Medium | Affects correct placement of economic buildings; distinguish footprint snapping from rendering. |
| 4 | [#897](https://github.com/CubeB/RWE-B4/issues/897) — Invite codes and host acceptance | High | Medium | Can block people from joining; first repair the documented flow and feedback before considering a transport rewrite. |
| 5 | [#894](https://github.com/CubeB/RWE-B4/issues/894) — Aircraft stopping in midair | High | Large | Combat and patrol can become unusable; start a captured mission-transition reproduction early, then fix the established cause. |
| 6 | [#896](https://github.com/CubeB/RWE-B4/issues/896) — Multiplayer lag / waiting stalls | High | Large | Can interrupt the whole match; start peer-log/timing capture early rather than guessing at lockstep changes. |
| 7 | [#902](https://github.com/CubeB/RWE-B4/issues/902) — Targets retained through fog | High | Medium | Potential fairness issue, but retained identity/last-known goals may be expected; compare the reference before choosing a fix. |
| 8 | [#900](https://github.com/CubeB/RWE-B4/issues/900) — Pathfinding report | High | Unknown | Important but not implementation-ready: the report has no route, unit or map. Capture one failure before estimating a fix. |
| 9 | [#903](https://github.com/CubeB/RWE-B4/issues/903) — Battleroom message-box text alignment | Normal | Small | Clearest localized quick-win candidate; reproduce the font/baseline mismatch and avoid changing global text layout blindly. |
| 10 | [#906](https://github.com/CubeB/RWE-B4/issues/906) — Command framework / permissions / repeat | Normal | Large | Unblocks cheat and developer commands. Deliver the ordinary-command routing slice first; developer gating and repeats need separate validation. |
| 11 | [#745](https://github.com/CubeB/RWE-B4/issues/745) — Smaller RWE downloads | Normal | Large | Begin with a package census and removable development content or a small engine-only package; replacing Electron is a separate large change. |
| 12 | [#904](https://github.com/CubeB/RWE-B4/issues/904) — Spectator underwater visibility | Normal | Medium | Restore the affected watcher view after identifying its mode; check for a shared cause with #899 without assuming one. |
| 13 | [#908](https://github.com/CubeB/RWE-B4/issues/908) — Audio and music commands | Normal | Medium | Bounded command family backed by existing audio services; validate track/category forms and singing assets. |
| 14 | [#907](https://github.com/CubeB/RWE-B4/issues/907) — Display / camera / input commands | Normal | Large | Start with the small existing-setting bindings such as +noshake, +clock and applicable option setters. Contours, dither and other missing effects make the full ticket large. |
| 15 | [#909](https://github.com/CubeB/RWE-B4/issues/909) — Resource and visibility sharing commands | Normal | Large | Verify the resource handlers first; radar/mapping toggles and shared sim state require reference resolution and multiplayer validation. |
| 16 | [#910](https://github.com/CubeB/RWE-B4/issues/910) — Targeting / colour / selectable / stockpile commands | Normal | Large | Changes can affect game outcomes; resolve player/argument rules and replicate mutations with save/hash/replay coverage. |
| 17 | [#912](https://github.com/CubeB/RWE-B4/issues/912) — ATM and NowISee | Normal | Medium | Implement after the mode/permission slice of #906; resource grants and reveal must respect the agreed multiplayer contract. |
| 18 | [#911](https://github.com/CubeB/RWE-B4/issues/911) — BPS and compression commands | Normal | Large | BPS is the smaller useful slice. Compression is a protocol/transport task and must not be estimated as a simple toggle. |
| 19 | [#914](https://github.com/CubeB/RWE-B4/issues/914) — DRDEATH and Roman-numeral player shortcuts | Low | Medium | Useful compatibility conveniences; first check whether the requested mission access/player counts already exist through the UI. |
| 20 | [#901](https://github.com/CubeB/RWE-B4/issues/901) — Nanolathe visual fidelity | Low | Medium | Cosmetic report without a specific mismatch; capture equivalent original/RWE views before changing particles. |
| 21 | [#915](https://github.com/CubeB/RWE-B4/issues/915) — MakePoster | Low | Large | Useful capture feature, but large-map rendering, output bounds and renderer restoration are substantial work. |
| 22 | [#913](https://github.com/CubeB/RWE-B4/issues/913) — Developer control / AI toggles / unit spawning | Low | Large | Broad ownership and multiplayer authority surface; depends on #906 gating and follows the basic gameplay and ordinary-command work. |

## Small changes to take early

- **#903:** battleroom baseline/alignment, once reproduced. It is the clearest
  candidate for a complete small fix, although its cosmetic impact is lower
  than the gameplay blockers.
- **#907, first slice:** route ordinary local commands and connect options that
  already exist. Verify the specific setting and original semantics before
  calling a command a quick wrapper. Do not bundle these with contour/dither
  implementation or estimate the full ticket as small.
- **#911, BPS slice:** expose existing network counters; retain the separate
  compression work and do not claim it complete through a notice/no-op.
- **#745, first slice:** measure the current packages and remove verified
  development-only content or evaluate an engine-only download. Do that before
  committing to an Electron replacement. The full issue remains Large.
- **#909, verification slice:** check existing metal/energy/reserve/gift
  handlers before rewriting them. Radar/mapping sharing remains larger work.

## Dependencies and evidence gates

- #912 and #913 need the agreed mode/permission part of #906. Small ordinary
  local-option bindings need the routing slice, not completion of every
  developer-mode feature. Do not block all command work behind the largest
  command ticket.
- #895 and #902 require a reference comparison before specifying a behavior
  change. #900 first needs a concrete reproduction. A retained fog target or
  a model overhanging an edge is not itself proof of a bug.
- #899 and #904 may share a water-rendering fault, but investigate the player
  and spectator modes separately until evidence establishes that connection.
- Preserve `needs-triage` until each ticket actually meets its implementation
  prerequisites. Priority and effort labels do not authorize speculative fixes.
- See the [playtest report](PLAYTEST-2026-10-26-REPORT.md) and
  [complete command inventory](CHAT-COMMAND-IMPLEMENTATION-BACKLOG.md) for scope,
  source limitations and acceptance requirements. ADR-0004 applies throughout.

## GitHub views

- [High-priority playtest issues](https://github.com/CubeB/RWE-B4/issues?q=is%3Aissue%20is%3Aopen%20label%3Apriority%3Ahigh)
- [Small issues](https://github.com/CubeB/RWE-B4/issues?q=is%3Aissue%20is%3Aopen%20label%3Aeffort%3Asmall)

The numbered table is the canonical order; labels support filtering on GitHub.
This prioritization changes no engine behavior and adds no runtime evidence.
