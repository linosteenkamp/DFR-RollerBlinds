# Domain Docs

This file tells the engineering skills how to use the domain documentation
of this repo when they examine the codebase.

## Before you examine the code, read these files

- **`CONTEXT.md`** at the repo root, or
- **`CONTEXT-MAP.md`** at the repo root, if it exists. It refers to one
  `CONTEXT.md` for each context. Read each `CONTEXT.md` that applies to
  the topic.
- **`docs/adr/`**. Read the ADRs for the area that you will work in. In a
  multi-context repo, also examine `src/<context>/docs/adr/` for decisions
  that apply to one context.

If one of these files does not exist, **continue and say nothing**. Do not
report that the file is missing. Do not recommend that somebody makes it
before it is necessary. The `/domain-modeling` skill (from
`/grill-with-docs` and `/improve-codebase-architecture`) makes these files
when a term or a decision is agreed.

## File structure

Single-context repo (most repos):

```
/
├── CONTEXT.md
├── docs/adr/
│   ├── 0001-event-sourced-orders.md
│   └── 0002-postgres-for-write-model.md
└── src/
```

Multi-context repo (the root has a `CONTEXT-MAP.md`):

```
/
├── CONTEXT-MAP.md
├── docs/adr/                          ← system-wide decisions
└── src/
    ├── ordering/
    │   ├── CONTEXT.md
    │   └── docs/adr/                  ← context-specific decisions
    └── billing/
        ├── CONTEXT.md
        └── docs/adr/
```

## Use the words of the glossary

When your output names a domain concept (in an issue title, a refactor
proposal, a hypothesis or a test name), use the term that `CONTEXT.md`
defines. Do not use a synonym that the glossary tells you not to use.

If the glossary does not have the concept that you need, there are two
possible causes:

- You make new words that the project does not use. Think again.
- The glossary has a real gap. Record the gap for `/domain-modeling`.

## Report conflicts with an ADR

If your output does not agree with an ADR, say so clearly. Do not
override the ADR without a statement:

> _Contradicts ADR-0007 (event-sourced orders) — but worth reopening because…_
