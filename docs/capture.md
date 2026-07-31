# Native runtime capture v1

OpenAGC runtime API v24 provides a diagnostic capture stream through
`openagc/capture.h`. Captures describe what the native runtime validated and
submitted; they are not replay files and must never be sent directly to a GPU.

## Application setup

The application owns the output destination and supplies an
`AgcCaptureWriteFunction`. The runtime makes synchronous, ordered callback
calls containing consecutive pieces of the stream.

```c
static void PS5_SYSV_ABI write_capture(
    void *user_data, const void *data, size_t size)
{
    FILE *output = user_data;
    (void)fwrite(data, 1, size, output);
}

AgcCaptureDesc desc = AGC_CAPTURE_DESC_INIT;
desc.write = write_capture;
desc.user_data = output;

AgcCapture capture = NULL;
if (agcCreateCapture(device, &desc, &capture) == AGC_OK) {
    (void)agcBeginCapture(capture);
    /* Create, record, submit, wait, and destroy application objects. */
    (void)agcEndCapture(capture);
    (void)agcDestroyCapture(capture);
}
```

Only one capture may be active on a device. Destroying an active capture
returns `AGC_ERROR_BUSY`. `agcGetCaptureInfo` reports active state, capture
status, records, bytes, and the next dense capture-local object ID. Capture
allocation or serialization failure is stored in that status and returned by
`agcEndCapture`; it does not turn an otherwise valid runtime/GPU operation
into failure.

Shader bytes are omitted by default. Setting
`AGC_CAPTURE_INCLUDE_SHADER_BYTES_BIT` is the explicit application opt-in for
a later shader-byte record; shader hashes and versions do not require it.

## Binary format

Every integer is unsigned little-endian unless a field is explicitly an
`AGC_ERROR_*` result encoded in the same 32 bits. Strings are fixed-size,
NUL-terminated when shorter than their field, and zero-padded. The stream does
not serialize C structure padding.

The 32-byte file header is:

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 8 | `OAGCCAP\0` |
| 8 | 4 | capture format version (`1`) |
| 12 | 4 | file-header size (`32`) |
| 16 | 4 | endian tag (`0x01020304`) |
| 20 | 4 | native runtime API version |
| 24 | 4 | capture flags |
| 28 | 4 | reserved zero |

Each record begins with a 16-byte header: 16-bit type, 16-bit record version,
32-bit total record size including the header, and a contiguous 64-bit
sequence number beginning at one. Unknown record types can be skipped by size;
an unknown record version must be rejected unless that version is documented.

The initial v1 writer emits runtime/profile capabilities, object creation and
debug names, matching destruction, command begin/end boundaries, application
PM4, final post-injection submitted PM4, submission command IDs and typed
wait/signal label points, bounded fence results, validation messages, and a
final authenticated record/byte count. Resource, shader/pipeline, transition,
and selected readback-hash records are extended in subsequent Milestone 5
slices using the same framing and stable capture-local IDs.

Raw host pointers and GPU addresses are never written by the runtime capture
API. PM4 dwords can themselves contain process-specific addresses, so the host
decoder redacts address-bearing and unknown register fields by default.

## Host decoder

Decode a stream without replaying it:

```sh
python3 tools/decode_openagc_capture.py frame.oagc
```

The decoder prints record order, object references, named PM4 packets, known
register names and safe values, submission dependencies, fence results, and
validation warnings. Address-bearing fields show `<redacted>` by default.
`--show-addresses` is an explicit diagnostic opt-in and should not be used when
sharing a capture.

The decoder rejects bad magic, unsupported versions, invalid sizes,
non-contiguous sequences, truncated command dwords, and malformed submission
tails. It performs no hardware access.
