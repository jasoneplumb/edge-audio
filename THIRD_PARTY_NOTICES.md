# Third-party notices

This repository is licensed under Apache-2.0 ([LICENSE](LICENSE), [NOTICE](NOTICE)). It **does not redistribute** any third-party source file, header, library, toolchain or binary. The only third-party-derived content is the ES8311 register sequence listed under "Incorporated material". Everything else below is software that you install yourself to build or test this repository, and each stays under its own license.

This file is an engineering inventory, not legal advice. Licenses were read from each project's repository in November 2026. Transitive dependencies are not listed.

## Incorporated material

| Source | License | What is used | Where |
| --- | --- | --- | --- |
| [M5Unified](https://github.com/m5stack/M5Unified) (M5Stack), `M5Unified::_microphone_enabled_cb_sticks3` in `M5Unified/src/M5Unified.inl`, compared at commit `96576a14247ddc935149145aa6b51943464120f1` | MIT | The eight ES8311 register/value pairs that enable the M5StickS3 microphone path. The values are identical; the surrounding C code is original. | `SG_ES8311_ENABLE_REGS` in `adapters/soundguardian/es8311_i2s.c` |

The register values are hardware configuration for the Everest Semiconductor ES8311 codec, and they may not be copyrightable on their own. They are attributed anyway, and M5Unified's notice is reproduced here:

```text
MIT License

Copyright (c) 2021 M5Stack

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Other code that looks third-party but is not:

- **`adapters/soundguardian/stubs/esp_stubs.h`** is a hand-written stand-in for the subset of the ESP-IDF API that the target adapter calls. Only the host syntax check (`target_stub_compile`) uses it. It reuses ESP-IDF's public function, type, field and macro names so that the adapter compiles unchanged, but it copies no ESP-IDF implementation or header text. ESP-IDF is Apache-2.0.
- **`adapters/alsa/`** was moved here from the author's own `rk3576-measurement-harness` repository and contains no third-party code. It calls alsa-lib through `<alsa/asoundlib.h>`.
- **`adapters/wav/`** is an original RIFF/WAVE reader.

## Target build (ESP-IDF adapter and example firmware)

Not committed or redistributed here. A firmware image built from `adapters/soundguardian/example` links ESP-IDF components. Anyone who distributes such an image takes on the notice obligations of those components.

| Project | Version | License | Use |
| --- | --- | --- | --- |
| [ESP-IDF](https://github.com/espressif/esp-idf) (Espressif Systems) | 5.5 (CI: `espressif/idf:v5.5.3` image) | Apache-2.0 | Framework, drivers (I2C master, I2S), logging, build system. It bundles third-party firmware components, including the FreeRTOS kernel (MIT), Newlib/Picolibc (BSD-style) and the Xtensa headers (MIT); its `docs/en/COPYRIGHT.rst` has the full list. |
| Espressif Xtensa toolchain (`xtensa-esp-elf`, GCC 14.2) | as pinned by ESP-IDF | GCC: GPL-3.0-or-later with the GCC Runtime Library Exception; binutils: GPL-3.0-or-later | Compiler for ESP32-S3. The runtime library exception means compiled output is not covered by the GPL. |
| [`espressif/idf` Docker image](https://hub.docker.com/r/espressif/idf) | `v5.5.3` | ESP-IDF and its tools, each under its own license | The CI `esp-idf` job only; nothing from the image is redistributed. |

## Host build and tests

| Project | Version | License | Use |
| --- | --- | --- | --- |
| [CMake](https://github.com/Kitware/CMake) and CTest | ≥ 3.21 | BSD-3-Clause | Host build and test runner |
| [Ninja](https://github.com/ninja-build/ninja) | any (optional) | Apache-2.0 | Optional CMake generator |
| C11 compiler: GCC or Clang | system | GCC: GPL-3.0-or-later with the GCC Runtime Library Exception; Clang/LLVM: Apache-2.0 with LLVM Exceptions | Host compilation |
| POSIX threads, C library, libm | system | platform-specific | Linked by the core library, tools and tests |
| [alsa-lib](https://github.com/alsa-project/alsa-lib) (`libasound2-dev` on Debian/Ubuntu) | system (optional) | LGPL-2.1-or-later | Linked dynamically by `adapters/alsa` and `test_alsa` when CMake finds it. Distributing a statically linked binary would carry LGPL obligations. |
| [PlatformIO Core](https://github.com/platformio/platformio-core) and the `native` platform | as resolved by pip (optional) | Apache-2.0 | The `native-smoke` environment in `platformio.ini` |
| Python | 3.12 in CI (optional) | PSF-2.0 | Installs and runs PlatformIO |
| [speccheck](https://github.com/jasoneplumb/speccheck) | optional | not stated by that repository | The README's optional citation walk. It is the author's own tool, called as an external program; nothing from it is included here. |

## CI and automation

| Project | License | Use |
| --- | --- | --- |
| [actions/checkout](https://github.com/actions/checkout), [actions/setup-python](https://github.com/actions/setup-python) | MIT | GitHub Actions steps in `.github/workflows/` |
| [anthropics/claude-code-action](https://github.com/anthropics/claude-code-action) | MIT | Pull-request review workflow (`claude-review.yml`) |

## Trademarks

Espressif, ESP32-S3 and ESP-IDF; M5Stack and M5StickS3; Everest Semiconductor and ES8311; Rockchip and RK3576; Linux and ALSA; PlatformIO; and other names belong to their owners. They are used only to identify the hardware and software involved. This project is not affiliated with, sponsored by or endorsed by any of them.
