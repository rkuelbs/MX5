# Logger Benchmark Results

`logger_benchmark` exercises the bounded asynchronous queue and compressed MDF4
writer with deterministic numeric values distributed across every signal in the
current DBC. It bypasses the decoded logging-rate policy so the requested rate
is the aggregate number of MDF samples per second.

## Windows Development Baseline

Date: 2026-07-14

Environment:

- Windows development PC
- CMake 4.4.0
- Ninja
- MinGW 13.1.0
- Qt 6.11.1
- mdflib v2.3.0, pinned commit `2eabc2b3ac89b4bf41d65dc0416ae1d34f1c1f16`
- Queue capacity: 65,536 records
- 65 decoded DBC signals
- Compressed MDF4 output

| Test | Records | Target rate | Producer time | Total time | Max queue | Dropped | MDF4 size |
|---|---:|---:|---:|---:|---:|---:|---:|
| Unthrottled maximum | 100,000 | simulated 10,000/s timestamps | 0.0707 s | 0.846 s | 54,206 | 0 | 1,287,794 bytes |
| Real-time sustained | 20,000 | 10,000/s | 2.000 s | 2.576 s | 6 | 0 | 306,013 bytes |

The unthrottled result measures how quickly this PC can enqueue records, not a
guaranteed storage rate. Total time includes queue draining, compression, and
MDF finalization. File sizes depend strongly on signal entropy and session
length, so these short synthetic results should not be used as vehicle storage
estimates.

## Raspberry Pi Qualification Still Required

Repeat sustained tests on the Raspberry Pi 5 using the intended vehicle storage
device. Test at expected and deliberately excessive CAN/VN300 loads, then repeat
with a nearly full filesystem and during a PDM-requested clean shutdown. Record:

- queue high-water mark and dropped records,
- CPU and memory use,
- storage write latency,
- finalization time,
- resulting file size,
- MDF readback success,
- behavior after forced power loss.
