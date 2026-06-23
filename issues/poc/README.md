# issues/poc/ — proof-of-concept reproducers

Runnable reproducers for the findings in `issues/`, one directory per component,
one file per finding ID (`scanner/scan-001.orb`, …). Re-run them as a fast
regression suite to confirm fixed bugs stay fixed.

```sh
# Rebuild the project first so bin/Orbit reflects current code, then:
issues/poc/run.sh            # run all components
issues/poc/run.sh scanner    # run one component
```

- **`.orb`** files declare their expectation in a `# EXPECT:` line
  (`ok` / `error` / `error: <substring>`).
- **`.cpp`** files are self-checking byte-level probes (exit 0 = pass), compiled
  on the fly against the built `libOrbiter`.

See [../GUIDE.md](../GUIDE.md) §11 for the full conventions (naming, kinds, when
*not* to write a PoC).
