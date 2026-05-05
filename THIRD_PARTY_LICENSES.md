# Third-Party Licenses

The Maize Procedural Generator includes or links against the following third-party components. Each component retains its own copyright and license; nothing below alters those terms.

---

## Dear ImGui

Vendored in `src/imgui*.cpp` and `include/imgui*.h, include/imstb_*.h, include/imconfig.h`.

- **Author:** Omar Cornut (2014–2025)
- **License:** MIT
- **Upstream:** https://github.com/ocornut/imgui

```
The MIT License (MIT)

Copyright (c) 2014-2025 Omar Cornut

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

---

## stb_image

Vendored in `include/stb_image/stb_image.h` and `include/stb_image/stb_image_write.h`.

- **Author:** Sean Barrett
- **License:** Dual-licensed under MIT or Public Domain (Unlicense) — choose either.
- **Upstream:** https://github.com/nothings/stb

The full dual-license text is at the bottom of `include/stb_image/stb_image.h`.

---

## freeglut

Linked dynamically via `freeglut.dll` (installed via vcpkg).

- **Authors:** Pawel W. Olszta and contributors
- **License:** MIT/X11
- **Upstream:** https://freeglut.sourceforge.net/

```
Copyright (c) 1999-2000 Pawel W. Olszta. All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies or substantial portions of the Software.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
PAWEL W. OLSZTA BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Pawel W. Olszta shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from Pawel W. Olszta.
```

The freeglut binary is redistributable under these terms; no restriction beyond preserving the copyright/permission notice.

---

## tinyxml2

Linked statically/dynamically via vcpkg.

- **Author:** Lee Thomason
- **License:** zlib
- **Upstream:** https://github.com/leethomason/tinyxml2

```
This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must
not claim that you wrote the original software. If you use this
software in a product, an acknowledgment in the product documentation
would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
```

---

## GLUI (vendored, no longer used)

The repository contains `lib/glui32.lib`, `lib/glui32d.lib`, and `include/GL/glui.h` from a previous version of the project. These files are **not used** by the current codebase (no source file includes `glui.h` or links against the library). They are retained only because they sit in the legacy repository tree.

- **Author:** Paul Rademacher (1998)
- **License:** zlib-style
- **Upstream:** https://sourceforge.net/projects/glui/

```
Copyright (c) 1998 Paul Rademacher

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from
the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software in
a product, an acknowledgment in the product documentation would be
appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not
be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

---

## OpenGL / GLU

System libraries provided by the operating system (Windows). No redistribution by this project.
