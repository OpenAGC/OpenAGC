# `sceAgcDriverFindResourcesPublic` ABI audit

## Result

The export behavior is proven, but its public C prototype is not. OpenAGC must
keep NID `5l3IfCFJxBs` unresolved rather than add a guessed declaration that
would make import-coverage reports misleading.

Every inspected PS5 `libSceAgcDriver.sprx`, from FW `1.00` through `12.70`,
exports the same six-byte body:

```asm
mov eax, 0x8a6c9018
ret
```

The function therefore ignores every incoming argument and modifies no caller
storage on all available firmware versions. This proves the runtime result but
cannot distinguish a zero-argument function from any fixed-argument resource
query signature.

## Dragon Quest VII Reimagined

The `PPSA17942` executable imports `5l3IfCFJxBs`, but does not call it. The
dynamic relocation resolves GOT slot `0x9490550` through PLT stub `0x669ac40`.
The only code reference is a title-local tail-jump thunk at `0x59b5cf0`.
There are no direct calls, tail calls, absolute pointers, RIP-relative address
loads, or dynamic `RELA` entries referring to that thunk.

Consequently, the title contributes no argument-register setup from which to
recover a prototype. The import is linked but dead.

## FW 5.50 system applications

Two FW `0x0550` system applications also import the NID:

- `NPXS40099`: local thunk `0x0b43a0`
- `NPXS40074`: local thunk `0x0bd160`

Both thunks tail-jump to their import PLT entries. Neither has a direct caller,
tail caller, absolute reference, RIP-relative reference, or relocated
function-pointer entry. Matching system applications from later firmware show
the same import pattern and do not provide a usable public prototype.

## Rejected evidence

The PS4-oriented sibling `opengnm` declares `sceGnmFindResourcesPublic(void)`,
but that declaration is a placeholder alongside other incompletely recovered
resource APIs. PS4 GNM is not sufficient evidence for the PS5 AGC ABI and must
not be copied into OpenAGC.

No exact declaration was found in the local PS5 SDK stubs, KytyPS5, SharpEmu,
RPCSX, or indexed source repositories. Generated SDK stubs provide the symbol
name only and contain no type information.

## Unblock condition

Implement the export only after obtaining at least one of:

- a live PS5 call site with unambiguous argument and output use;
- a PS5 header containing the complete declaration; or
- a non-stub PS5 implementation that reveals the parameter contract.

Until then, the exact constant-return behavior remains documented evidence, not
a public OpenAGC function declaration.
