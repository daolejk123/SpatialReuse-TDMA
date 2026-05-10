---
name: verify-citations
description: Validate academic references in Markdown, text, PDF-derived text, LaTeX, BibTeX, literature reviews, research notes, or related-work drafts. Use when the user asks to verify citations, detect hallucinated papers, check bibliography authenticity, or produce a cleaned document with fake references removed.
---

# Citation Verification

Use this skill when asked to verify whether references in a document are real. Prefer `academic-refchecker`; do not maintain older ad hoc scripts unless the user explicitly asks for an offline/custom checker.

## Workflow

1. Check/install the tool:

```bash
source /home/opp_env/omnetpp-6.3.0/setenv 2>/dev/null
which academic-refchecker &>/dev/null && echo "rc=ok" || pip install academic-refchecker
```

If install fails due to PEP 668, activate the OMNeT++ venv first with `source /home/opp_env/omnetpp-6.3.0/setenv`, then retry.

2. Extract references from the user-provided input into `/tmp/papers_to_verify.bib`.

- For Markdown tables, extract title, author, year, and venue/booktitle.
- For LaTeX projects, use an existing `.bib` when available.
- For numbered text lists, split on `[N]` or `N.` entries.
- Preserve suspicious metadata such as `Various`; write `et al.`/`others` as `and others`.
- Do not correct titles, authors, years, or venues before checking.

3. Run refchecker:

```bash
source /home/opp_env/omnetpp-6.3.0/setenv && \
academic-refchecker --paper /tmp/papers_to_verify.bib \
  --output-file /tmp/refchecker_output.txt \
  --report-file /tmp/refchecker_report.json \
  --report-format json
```

If entries fail with `rate_limited_or_timeout`, rerun those entries separately before marking them fake.

4. Write `docs/引用验证-refchecker报告.md`.

Include a summary table, per-reference claimed metadata versus actual/corrected metadata, error type, verdict, and final action list.

Verdict mapping:

- No `❌`, `⚠️`, or `🚩`: verified.
- Only year/venue warnings: metadata fix needed.
- First-author mismatch: serious metadata error.
- `🚩 Total likely hallucinated` or persistent `Paper not found by any checker`: likely fake; do not cite without manual evidence.

5. If the user asks for a cleaned version, create `docs/<original-name>-修正版.md` without overwriting the original.

Delete sections tied to likely fake references, correct serious metadata errors using refchecker `CORRECTED REFERENCE`, keep verified or warning-only items, and add a short correction note plus an appendix verification table.

## Hallucination Signals

Explicitly call out likely AI citation grafting when you see `Various` as first author, future years, famous authors attached to unrelated papers, or real titles paired with inflated venues.

## Outputs

- `/tmp/papers_to_verify.bib`
- `/tmp/refchecker_output.txt`
- `/tmp/refchecker_report.json`
- `docs/引用验证-refchecker报告.md`
- Optional `docs/<原文件名>-修正版.md`
