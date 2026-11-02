# Cost multiplier example

The worked example for the first mod SDK. It scales every unit's metal and
energy cost by a manifest option, leaving build time alone.

## Preview against real data

```sh
rwe_setup --validate-mod examples/mod-cost-multiplier/rwe_mod.json
rwe_setup --preview-mods examples/mod-cost-multiplier/rwe_mod.json \
    --data-path <data directory> \
    --option com.example.costmultiplier:multiplier=2.0
```

The preview prints the resolved-set identity, the unit count, and every
`before`/`after` pair. It writes nothing.

## The API this uses

| Call | Meaning |
| --- | --- |
| `rwe.unit_names()` | Every loaded unit name, sorted. |
| `rwe.unit_get(name, key)` | The current value of `MetalCost`, `EnergyCost` or `BuildTime`. |
| `rwe.unit_set(name, key, value)` | A new value, rounded once at the end. |
| `rwe.options` | Resolved manifest options. |
| `rwe.log(text)` | A line in the preview's log. |

Writes are atomic per script: an error restores every value the script had
already written. Unknown units and keys are errors, not silent no-ops.

## What is deliberately not here

No clock, filesystem, network, `require`, `os`, `io`, `debug`, `load` or
`math.random` is reachable. Format 1 exposes three integer cost keys only.
This is a pre-match transform: no Lua state exists during the battle.

See `docs/MODDING-SDK-FINDINGS.md` MOD-SDK-004 and MOD-SDK-005 for the
contract and its present limits.
