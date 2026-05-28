# SearchPlusPlus examples

Small self-contained programs that show how to use the SearchPlusPlus C++
library directly, without going through the HTTP server.

| Example | What it shows |
|---|---|
| [`hello_search`](hello_search/main.cpp) | The smallest end-to-end flow: declare a schema, open an index in a temp dir, add a few documents, refresh, parse a query, run it through `Searcher`, print the top hits with scores. |

Build (examples are on by default):

```bash
cmake --preset default
cmake --build --preset default --target hello_search
./build/default/examples/hello_search/hello_search
```

To turn examples off (e.g. when this repo is used as a CMake subproject):

```bash
cmake --preset default -DSPP_BUILD_EXAMPLES=OFF
```

## Using SearchPlusPlus from your own CMake project

The examples link against the `spp_core` target, which is what you'd link
against from any other CMake target in the same build tree:

```cmake
target_link_libraries(your_target PRIVATE spp_core)
```

Public headers are under `include/spp/...` and are reachable as
`#include "spp/<module>/<header>.h"`. The four entry-point headers most
programs need are:

- `spp/index/schema.h` — field declarations
- `spp/index/index_writer.h` — opening, AddDocument, Refresh, Close
- `spp/query/query_parser.h` — parse a query string to an AST
- `spp/query/searcher.h` — run an AST against an `IndexReader`
