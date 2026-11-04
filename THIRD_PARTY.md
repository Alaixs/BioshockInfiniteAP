# Third-party code

| Component | Where | License |
| --- | --- | --- |
| [MinHook](https://github.com/TsudaKageyu/minhook) | `mod/extern/minhook` | BSD 2-Clause, see `mod/extern/minhook/LICENSE.txt` |
| [miniz](https://github.com/richgel999/miniz) | `mod/extern/miniz` | MIT, see `mod/extern/miniz/LICENSE` |
| [JSON for Modern C++](https://github.com/nlohmann/json) | `mod/extern/nlohmann/json.hpp` | MIT, see the header of the file |
| [bioshock-infinite-headtracking](https://github.com/itsloopyo/bioshock-infinite-headtracking) | `mod/src/xinput_proxy.cpp`, `mod/src/xinput1_3.def`, part of `mod/src/game_build.h` | MIT, below |

The xinput proxy and the first addresses and offsets of the Steam build were taken from
bioshock-infinite-headtracking:

```
MIT License

Copyright (c) 2026 itsloopyo

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
