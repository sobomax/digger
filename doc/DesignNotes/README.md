# Design Notes

## Message Memory

Winking allocation scheme is employed to dynamically allocate structures
that are emitted during parsing process. All dynamic memory requirements of
the parser are fulfilled using pre-allocated array that is co-located with
on-wire message buffer and deallocated in one shot.

Original message is copied at the very beginning of the allocate buffer,
followed by the empty "message heap" area. As message parsing progressing,
this area is filled with data structures, for the most part referencing parts
of the message area.

Internal API is provided to perform speculative reservation into the heap
area and return space back to heap in case of parsing errors.

It is conjectured that such design results in an extremely efficient use
of L1 cache memories, which are in the order of few tenths of kilobytes
per core.

## Fast Header-Field Names Parser

```
[On-wire HF Name] -=7 bit=-> [De-capitalization / Bit-compaction] -=5 bit=->
  [Pre-computed Pearson Hash using 64-entries array] -=Canonical Header Id=->
  [Verification using strcasencmp()].
```
