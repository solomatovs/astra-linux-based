#!/usr/bin/env python3
import os
import re
import sys


def mirrors(gitdeps):
    for owner in sorted(os.listdir(gitdeps)):
        path = os.path.join(gitdeps, owner)
        if not os.path.isdir(path):
            continue
        for repo in sorted(os.listdir(path)):
            if repo.endswith(".git"):
                yield owner, repo[: -len(".git")]


def manifests(src):
    for root, dirs, files in os.walk(src):
        if "node_modules" in dirs:
            dirs.remove("node_modules")
        for name in ("package.json", "package-lock.json"):
            if name in files:
                yield os.path.join(root, name)


def main():
    src, gitdeps = sys.argv[1], sys.argv[2]

    rules = []
    for owner, repo in mirrors(gitdeps):
        rules.append(
            (
                re.compile(
                    r'"(?:git\+)?'
                    r"(?:ssh://git@github\.com/|git://github\.com/"
                    r'|https://github\.com/|github:)?'
                    + re.escape("%s/%s" % (owner, repo))
                    + r'(?:\.git)?(#[^"]*)?"'
                ),
                "%s/%s/%s.git" % (gitdeps, owner, repo),
            )
        )

    if not rules:
        sys.exit("нет зеркал в %s" % gitdeps)

    total = 0
    for path in manifests(src):
        with open(path, encoding="utf-8") as fh:
            before = fh.read()

        after = before
        for pattern, target in rules:
            after = pattern.sub(
                lambda m, t=target: '"git+file://%s%s"' % (t, m.group(1) or ""), after
            )

        if after != before:
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(after)
            print("переписан %s" % path)
            total += 1

    print("зеркал: %d, файлов переписано: %d" % (len(rules), total))


main()
