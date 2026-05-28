# Third-party licenses

This project bundles a few third-party components. Each is the work of its
original author and stays under its own license. Their license texts are kept
next to the code, and the full inventory is below.

| Component | Author / Upstream | License | License file |
|---|---|---|---|
| Dobby | jmpews — https://github.com/jmpews/Dobby | Apache-2.0 | `app/src/main/jni/Dobby/LICENSE` |
| KittyMemory | MJx0 — https://github.com/MJx0/KittyMemory | MIT | `app/src/main/jni/KittyMemory/LICENSE` |
| Keystone Engine | Nguyen Anh Quynh — https://github.com/keystone-engine/keystone | GPL-2.0 | `app/src/main/jni/KittyMemory/Deps/Keystone/COPYING` |
| xDL | HexHacking Team — https://github.com/hexhacking/xDL | MIT | notice in the header of each `app/src/main/jni/xDL/*` source file |
| Obfuscate (`obfuscate.h`) | Adam Yaxley — https://github.com/adamyaxley/Obfuscate | Public Domain (Unlicense) | notice at the end of `app/src/main/jni/Includes/obfuscate.h` |

The project's own code is licensed under GPL-3.0, see `LICENSE`.

Note: Keystone is GPL-2.0. It is only used for assembling instructions at
runtime in the patch helpers. If you ever need to avoid GPL-2.0 entirely,
drop Keystone and stick to hex patches.
