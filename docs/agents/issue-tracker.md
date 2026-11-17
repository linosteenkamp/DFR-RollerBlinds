# Issue tracker: GitHub

The issues and PRDs for this repo are GitHub issues. Use the `gh` CLI for
all operations.

## Conventions

- **Create an issue**: `gh issue create --title "..." --body "..."`. For a
  body with more than one line, use a heredoc.
- **Read an issue**: `gh issue view <number> --comments`. Use `jq` to
  filter the comments, and also get the labels.
- **List issues**: `gh issue list --state open --json number,title,body,labels,comments --jq '[.[] | {number, title, body, labels: [.labels[].name], comments: [.comments[].body]}]'`.
  Add the necessary `--label` and `--state` filters.
- **Comment on an issue**: `gh issue comment <number> --body "..."`
- **Add or remove labels**: `gh issue edit <number> --add-label "..."` / `--remove-label "..."`
- **Close**: `gh issue close <number> --comment "..."`

Get the repo from `git remote -v`. When you run `gh` in a clone, it does
this automatically.

## Pull requests as a triage surface

**PRs as a request surface: no.** _(Set this to `yes` if this repo uses
external PRs as feature requests. `/triage` reads this flag.)_

When the flag is `yes`, PRs use the same labels and states as issues. Use
the `gh pr` commands that agree with the issue commands:

- **Read a PR**: `gh pr view <number> --comments`. For the diff, use
  `gh pr diff <number>`.
- **List external PRs for triage**: run
  `gh pr list --state open --json number,title,body,labels,author,authorAssociation,comments`.
  Keep only the PRs with an `authorAssociation` of `CONTRIBUTOR`,
  `FIRST_TIME_CONTRIBUTOR` or `NONE`. Remove `OWNER`, `MEMBER` and
  `COLLABORATOR`.
- **Comment, label or close**: `gh pr comment`, `gh pr edit --add-label` /
  `--remove-label`, `gh pr close`.

Issues and PRs on GitHub use one set of numbers. Thus `#42` can be an
issue or a PR. To find which it is, run `gh pr view 42`. If that fails,
run `gh issue view 42`.

## When a skill says "publish to the issue tracker"

Create a GitHub issue.

## When a skill says "fetch the relevant ticket"

Run `gh issue view <number> --comments`.

## Wayfinding operations

`/wayfinder` uses these operations. The **map** is one issue. Its
**child** issues are the tickets.

- **Map**: one issue with the label `wayfinder:map`. Its body holds the
  Notes, Decisions-so-far and Fog sections. Create it with
  `gh issue create --label wayfinder:map`.
- **Child ticket**: an issue that is linked to the map as a GitHub
  sub-issue (use `gh api` on the sub-issues endpoint). If sub-issues are
  not enabled, add the child to a task list in the map body. Then put
  `Part of #<map>` at the top of the child body. Labels:
  `wayfinder:<type>` (`research`, `prototype`, `grilling` or `task`).
  When a developer claims the ticket, it is assigned to that developer.
- **Blocking**: use GitHub's **native issue dependencies**. This is the
  standard representation, and the UI shows it. To add an edge, run
  `gh api --method POST repos/<owner>/<repo>/issues/<child>/dependencies/blocked_by -F issue_id=<blocker-db-id>`.
  `<blocker-db-id>` is the numeric **database id** of the blocker
  (`gh api repos/<owner>/<repo>/issues/<n> --jq .id`). It is _not_ the
  `#number` or the `node_id`. GitHub gives
  `issue_dependencies_summary.blocked_by`, which counts only the open
  blockers. This count is the live gate. If dependencies are not
  available, put a `Blocked by: #<n>, #<n>` line at the top of the child
  body. A ticket is unblocked when all its blockers are closed.
- **Frontier query**: list the open children of the map
  (`gh issue list --state open`, limited to the sub-issues or task list of
  the map). Remove each child that has an open blocker
  (`issue_dependencies_summary.blocked_by > 0`, or an open issue in the
  `Blocked by` line). Also remove each child that has an assignee. The
  first child in map order is the next ticket.
- **Claim**: `gh issue edit <n> --add-assignee @me`. This is the first
  write of the session.
- **Resolve**: run `gh issue comment <n> --body "<answer>"`, then
  `gh issue close <n>`. Then add a context pointer (a summary and a link)
  to the Decisions-so-far section of the map.
