# Copilot instructions for syntax-the-imperiled

## Build and validation

Use the host-safe test target first:

```bash
g++ -std=c++17 -O2 src/overclock.spec.cpp src/animation.cpp -o overclock.spec
./overclock.spec
```

Use the simulator only for visual checks:

```bash
g++ -std=c++17 -O2 src/simulator.cpp src/animation.cpp -o simulator
./simulator
```

## Architecture

- Shared animation logic lives in `src/animation.cpp` and `src/animation.h`.
- Raspberry Pi runtime and hardware glue live in `src/overclock.cpp`.
- `src/simulator.cpp` is a host-side preview tool; it should reuse the animation engine, not duplicate it.
- `src/overclock.spec.cpp` is the source of truth for timing and behavior verification.

Keep these boundaries strict: do not mix hardware runtime code with the simulator or the deterministic tests.

## Repo conventions

- Change thresholds and timings in `src/animation.cpp` and keep the related assertions in `src/overclock.spec.cpp` in sync.
- Prefer the shared animation engine over inlining behavior in the runtime path.
- Keep SPI/GPIO/RPi details isolated from host-safe logic.
- Validate animation changes with the spec runner before visual-only checks.
