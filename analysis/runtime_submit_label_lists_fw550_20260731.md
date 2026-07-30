# FW 5.50 native runtime submit-list oracle

Date: 2026-07-31

Target: standard PS5, system software raw `0x05500008`, normalized ABI key
`0x0550`, runtime profile `prospero-gc-submit16-standard-standard`.

Artifact SHA-256:

```
4e3f0e5996e9912a24ac476862c15901c3e4512b3e2fa19ec78df2bebef9d4e2
```

The public native-runtime sample `agc_runtime_submit_lists.elf` used no
application shader, descriptor, draw, dispatch, or resource state. It
submitted three compute DCBs without CPU waits between them:

1. A command-buffer-recorded EOP signal of source label value 1.
2. A v2 `AgcSubmitInfo` bridge with one submit-level exact wait on value 1 and
   one submit-level EOP signal of destination label value 2.
3. A v2 `AgcSubmitInfo` consumer with one submit-level exact wait on value 2.

All three bounded fence waits returned `AGC_OK`, followed by command-buffer
reset and complete destruction of all fences, labels, queue, and device.

This qualifies the exact single-command-buffer compute submit-list path on FW
5.50 standard PS5: one preamble wait and one tail signal, plus a later wait on
that signal. It does not qualify graphics multi-command batches, multiple list
elements, resource ownership semantics beyond their existing qualification,
timeline rollover, or FW 11.60.
