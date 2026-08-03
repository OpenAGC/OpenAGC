#!/usr/bin/env python3
"""Keep registered PS5 scanout memory owned until VideoOut releases it."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def function_body(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


prospero = (ROOT / "src/videoout_prospero.c").read_text()
close_body = function_body(
    prospero, "int32_t agcVideoOutCloseChecked", "void agcVideoOutClose")
ordered_calls = (
    "sceVideoOutDeleteFlipEvent",
    "sceVideoOutUnregisterBuffers",
    "sceVideoOutClose",
    "sceKernelDeleteEqueue",
    "free(video_out)",
)
positions = [close_body.index(call) for call in ordered_calls]
assert positions == sorted(positions), "unsafe Prospero VideoOut teardown order"
assert "return AGC_ERROR_INTERNAL" in prospero
for stage in (
    "delete-flip-event",
    "unregister-buffers",
    "close-handle",
    "delete-equeue",
):
    assert f'videoout_teardown_error("{stage}", native_result)' in close_body
assert "buffers_registered = false" in close_body
busy = close_body.index("SCE_VIDEO_OUT_ERROR_RESOURCE_BUSY")
checked_close = close_body.index("sceVideoOutClose(video_out->handle)", busy)
release_after_close = close_body.index(
    "video_out->buffers_registered = false", checked_close
)
assert busy < checked_close < release_after_close

runtime = (ROOT / "src/runtime.c").read_text()
destroy_body = function_body(
    runtime, "int32_t PS5_SYSV_ABI agcDestroyPresentChain", "int32_t PS5_SYSV_ABI agcPresent")
checked = destroy_body.index("agcVideoOutCloseChecked")
result_gate = destroy_body.index("if (result != AGC_OK)", checked)
dependency_release = destroy_body.index("dependency_refs--", result_gate)
assert checked < result_gate < dependency_release

print("Prospero VideoOut teardown ownership: PASS")
