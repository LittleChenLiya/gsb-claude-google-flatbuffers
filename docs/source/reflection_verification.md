# Structured Verification Reports (Reflection)

The regular `flatbuffers::Verifier` and the reflection-based
`flatbuffers::Verify()` answer a single boolean: is this buffer valid?
When a buffer fails verification they do not tell you *where* or *why*.

`flatbuffers::VerifyWithReport()` (declared in
`include/flatbuffers/reflection.h`, implemented in `src/reflection.cpp`)
verifies a buffer against a binary schema (`.bfbs`, see
[Intermediate Representation](intermediate_representation.md)) and, instead
of a plain boolean, returns a `flatbuffers::ReflectionVerifyReport`
describing the **first** failure:

```cpp
struct ReflectionVerifyReport {
  bool ok;                       // did the buffer pass verification?
  bool budget_exceeded;          // stopped early due to the work budget?
  ReflectionVerifyFailure failure;
  std::string ToString() const;  // one-line human readable summary
};

struct ReflectionVerifyFailure {
  ReflectionVerifyError category;  // what kind of failure
  std::string path;        // e.g. "testarrayoftables[1].name"
  size_t byte_offset;      // failing location, relative to buffer start
  size_t vector_index;     // innermost vector index, or
                           // kReflectionVerifyNoVectorIndex
};
```

## Usage

```cpp
auto& schema = *reflection::GetSchema(bfbs_data);
auto report = flatbuffers::VerifyWithReport(
    schema, *schema.root_table(), buf, length);
if (!report.ok) {
  // e.g. "verification failed: string not terminated at path
  //       'testarrayoftables[1].name' (byte offset 174, vector index 1)"
  Log(report.ToString());
}
```

A size-prefixed variant, `flatbuffers::VerifySizePrefixedWithReport()`, is
also provided. The original boolean `flatbuffers::Verify()` and
`flatbuffers::VerifySizePrefixed()` interfaces are unchanged and remain
available.

## Error categories

| Category | Meaning |
|---|---|
| `kBufferTooSmall` | buffer smaller than the minimum flatbuffer size |
| `kOutOfBounds` | a read would go past the end of the buffer |
| `kMisaligned` | data is not aligned to its required alignment |
| `kInvalidOffset` | an offset is 0, negative, or points out of the buffer |
| `kMalformedVTable` | a vtable is malformed |
| `kMissingRequiredField` | a required field is absent |
| `kStringNotTerminated` | a string is missing its NUL terminator |
| `kUnionTypeMismatch` | union discriminator and value are inconsistent |
| `kDepthLimitExceeded` | nesting deeper than `max_depth` |
| `kTableLimitExceeded` | more tables than `max_tables` |
| `kBudgetExceeded` | work budget exhausted before completing |

## Guarantees

*   **Same limits as `Verifier`.** All bounds and alignment checks go
    through a regular `flatbuffers::Verifier`, and the depth / table-count
    limits mirror `Verifier::VerifyComplexity()`. No pointer into the
    buffer is dereferenced before the memory it refers to has been
    verified, so corrupt offsets, truncated (including zero-length) inputs
    and shared vtables are handled safely.
*   **Unions are checked as a pair.** The discriminator (`<name>_type`)
    and the value are validated together: an out-of-range discriminator, a
    discriminator of `NONE` with a value present, or a discriminator with
    no matching value are all reported as `kUnionTypeMismatch`, and the
    object is only ever interpreted as the type the discriminator selects.
*   **Termination on repeated references.** A table that is referenced
    more than once (a DAG) is verified only once, so verification always
    terminates. The reported path is the path of the visit that actually
    detected the failure.
*   **Bounded report construction.** The failure path is capped both in
    the number of segments and in the length of each segment (a truncated
    path ends in `/...`), so even a failure at the end of a very deep
    chain does not cause unbounded path reconstruction.

## Work budget

`ReflectionVerifyOptions::max_work` optionally limits the number of
verification steps (roughly one per visited table, field and vector
element). When the budget is exhausted, verification stops early and the
report is flagged separately: `ok == false`, `budget_exceeded == true` and
`failure.category == kBudgetExceeded`. A budget-exceeded report therefore
cannot be confused with an actual content failure of the buffer.

```cpp
flatbuffers::ReflectionVerifyOptions opts;
opts.max_depth = 64;        // same defaults as Verifier
opts.max_tables = 1000000;
opts.max_work = 10000;      // 0 = unlimited
auto report = flatbuffers::VerifyWithReport(schema, root, buf, len, opts);
```

## Example

A complete runnable example that builds a buffer, corrupts it in several
ways and prints the field path and error location of each failure is
available at `samples/sample_reflection_report.cpp` (built as the
`flatsamplereflectionreport` target when tests are enabled).
