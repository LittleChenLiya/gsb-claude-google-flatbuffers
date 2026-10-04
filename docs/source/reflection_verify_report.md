# Reflection Verification with Failure Reporting

## Overview

The reflection-based verifier can now produce a structured failure report
instead of a simple boolean result. When a buffer fails verification, the
report identifies:

- **Error category** — what kind of failure occurred (out of bounds, invalid
  offset, missing required field, etc.).
- **Field path** — the table field and vector index where the failure was
  detected, e.g. `root.enemy.testarrayofstring[2]`.
- **Byte offset** — the relative offset inside the buffer where the failure
  was found.
- **Work budget flag** — whether the failure was caused by exceeding the
  caller-specified work budget rather than a structural corruption.

The original boolean `Verify` interface continues to work unchanged.

## API

```cpp
#include "flatbuffers/reflection.h"

flatbuffers::VerifyOptions opts;
opts.max_depth = 64;        // maximum nesting depth (default 64)
opts.max_tables = 1000000;  // maximum number of tables (default 1000000)
opts.max_work = 0;          // work budget: max tables + vector elements +
                            // fields to visit. 0 = unlimited (default).

auto result = flatbuffers::VerifyWithReport(schema, root_table, buf, len, opts);
// or for size-prefixed buffers:
// auto result = flatbuffers::VerifySizePrefixedWithReport(schema, root_table, buf, len, opts);

if (!result.ok) {
  printf("verification failed: %s at %s (offset %zu)\n",
         result.failure.ErrorName(),
         result.failure.PathString().c_str(),
         result.failure.byte_offset);
}
```

### VerifyResult

| Field | Type | Description |
|---|---|---|
| `ok` | `bool` | `true` if the buffer verified successfully. |
| `failure` | `VerifyFailure` | Details of the first failure. |

### VerifyFailure

| Field | Type | Description |
|---|---|---|
| `error` | `VerifyError` | Error category enum value. |
| `path` | `std::vector<VerifyPathStep>` | Field path from root to failure. |
| `byte_offset` | `size_t` | Relative byte offset of the failure. |
| `message` | `std::string` | Human-readable description. |
| `work_budget_exceeded` | `bool` | `true` if the failure is a work-budget cap. |

`PathString()` returns the full path, e.g. `root.enemy.testarrayofstring[2]`.
`ErrorName()` returns a short string name for the error category.

### VerifyError categories

| Enum value | ErrorName | Description |
|---|---|---|
| `kNone` | `none` | No error. |
| `kOutOfBounds` | `out_of_bounds` | A read extends past the buffer end. |
| `kAlignment` | `alignment` | A scalar is misaligned. |
| `kInvalidVTable` | `invalid_vtable` | A vtable is malformed. |
| `kInvalidOffset` | `invalid_offset` | A uoffset_t points outside the buffer. |
| `kMissingRequired` | `missing_required` | A required field is absent. |
| `kInvalidUnionType` | `invalid_union_type` | A union discriminator is out of range. |
| `kUnionValueMissing` | `union_value_missing` | A union value pointer is null. |
| `kStringNotTerminated` | `string_not_terminated` | A string is missing its null terminator. |
| `kMaxDepthExceeded` | `max_depth_exceeded` | Nesting depth exceeds `max_depth`. |
| `kMaxTablesExceeded` | `max_tables_exceeded` | Table count exceeds `max_tables`. |
| `kWorkBudgetExceeded` | `work_budget_exceeded` | Work budget (`max_work`) was exhausted. |
| `kBufferTooSmall` | `buffer_too_small` | Buffer is smaller than the minimum FlatBuffer size. |

## Work budget

The `max_work` option lets callers bound the amount of work the verifier
does. The budget counts:

1. Every table visited.
2. Every vector element visited.
3. Every field checked.

When the budget is exhausted, verification stops and reports
`kWorkBudgetExceeded` with `work_budget_exceeded = true`. This is reported
separately from structural corruption so callers can distinguish "the buffer
is too complex" from "the buffer is corrupt".

The report construction itself obeys the work budget — the path is built
incrementally as the verifier descends, so a deep-chain failure does not
trigger an unbounded path reconstruction after the failure is found.

## Safety properties

- **Bounds and depth limits** — the report verifier uses the same `Verifier`
  bounds and depth checks as the boolean `Verify`.
- **No unverified pointer reads** — every pointer is range-checked before it
  is dereferenced.
- **Union discriminator and value checked together** — the union type byte and
  the union value pointer are validated as a pair.
- **Repeated object references terminate** — a visited set prevents infinite
  loops on DAGs or cycles. The actual failed access path is preserved because
  the path is tracked independently of the visited set.

## Example

See `examples/verify_report_example.cpp` for a complete program that builds a
buffer, corrupts it, and prints the field path and error location.
