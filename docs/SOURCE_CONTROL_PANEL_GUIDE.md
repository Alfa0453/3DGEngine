# Source Control Panel Guide

The Source Control Panel provides a safe, local Git workflow inside 3DG Editor. Open
it from **Panels > Content > Source Control**. It automatically searches upward from
the active project file for the repository root. If the project is not inside a Git
repository, the panel reports that state without changing the project.

## Changes

The Changes tab shows every modified, added, deleted, renamed, untracked, or
conflicted file reported by Git. The two-letter state is Git's index and working-tree
status; **Staged** means the index contains a version ready to commit. Conflicts are
shown in red and summarized above the file list.

Use the filter or an active editor change list to narrow the view. Select one or more
files, then:

- **Stage Selected** adds those paths to Git's index.
- **Unstage Selected** removes those paths from the index without deleting edits.
- **View Diff** opens either the working-tree or staged diff, depending on **Staged Diff**.
- **Commit Staged** commits the current Git index with the entered message.

These operations run in the background so a large repository does not freeze the
editor. Refresh retrieves the latest local status, branches, and history.

## History and Branches

History lists the latest 100 local commits. Select a commit to inspect its decorated
summary and file statistics. This inspection also runs in the background.

Branches lists local branches, the current branch, configured upstreams, and tracking
state. Select a different branch and choose **Switch to Selected**, or enter a new
name and choose **Create + Switch**. The editor does not force a switch: Git refuses
the request if it would overwrite uncommitted work.

## Editor Change Lists

Change lists are lightweight editor groupings for related changed files. They do not
alter Git branches, commits, or staging. Select files on the Changes tab, create or
select a change list, and choose **Add Selected Changed Files**. Choosing a list makes
the Changes tab show only its members; choose **All Changes** to remove the filter.

Change lists are stored at:

`<Project>/Saved/SourceControlChangelists.txt`

The `Saved` location keeps this organizational state separate from authored game
content. Deleting a change list never deletes or reverts its files.

## Safety Scope

The panel deliberately excludes destructive operations such as discard, hard reset,
forced checkout, branch deletion, and repository cleanup. Resolve conflicted file
contents in the appropriate editor, then stage the resolved paths here. Remote network
operations such as fetch, pull, and push remain outside this initial local workflow.
