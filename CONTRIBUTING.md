# Contributing

Thanks for your interest in `gsusb-canfd`. Contributions of all kinds are welcome:
bug reports, feature requests, documentation fixes and pull requests.

## Project layout

```
include/canfd/   public C++ headers and the C ABI (canfd.h)
src/             C++ library implementation
tools/           canfd CLI and interactive terminal
examples/        C++ examples
tests/           C++ unit tests
python/          pure-Python package (gsusb_canfd), CLI and tests
scripts/         helper build scripts
```

The C++ and Python implementations share the same wire protocol and API. When you
change protocol behaviour (control requests, bit timing, DLC mapping, frame
layout), please update both and add/adjust tests in both.

## Building and testing

### C++

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Requires CMake >= 3.16, a C++17 compiler and libusb-1.0 (see the README for how
it is discovered).

### Python

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -e './python[dev]'
pytest python/tests
```

## Style

- C++: C++17, 2-space indentation, `-Wall -Wextra` clean, no comments unless they
  explain non-obvious behaviour.
- Python: 4-space indentation, type hints on public APIs, keep protocol logic in
  `protocol.py` free of `pyusb` imports so it stays testable without hardware.
- Formatting follows `.editorconfig`; keep lines reasonably short.

## Hardware

Some behaviour (endpoints, CAN FD data-phase timing) can only be validated on a
real adapter. If you cannot test on hardware, say so in the PR — hardware-free
unit tests are still required, and a maintainer can run the hardware checks.

## Pull requests

1. Fork the repository and create a topic branch.
2. Make focused commits with clear messages.
3. Ensure both test suites pass and add tests for new behaviour.
4. Update the README/CHANGELOG when user-facing behaviour changes.
5. Open a PR using the template and describe what you tested and how.

## Releases

Releases are published from GitHub Actions with Trusted Publishing; see
[`python/PUBLISHING.md`](python/PUBLISHING.md). The version has a single source of
truth in `python/src/gsusb_canfd/_version.py` — CMake reads it and generates
`canfd/version.hpp` for the C++/C ABI, so bump that one file only.

## License

By contributing you agree that your contributions are licensed under the MIT
License (see [LICENSE](LICENSE)).
