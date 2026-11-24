"""Release notes for a version: the "## v<version>" section of CHANGELOG.md, made for a GitHub Release.

  python -I Tools/release_notes.py [<version>] [<out file>]     (default: version from app_info.toml, stdout)

GitHub shows every line break in a release text, so the wrapped lines of each paragraph / list item are
joined into one line (blank lines, headings, list items, tables, quotes and code blocks are kept). A fixed
"how to start" line is added at the end. Exit code 1 if CHANGELOG.md has no section for the version.
Used by .github/workflows/release.yml (the release text) and check.yml (shown on release branches).
"""
import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
FOOTER = ("Unzip, put your own ROMs into `ROMs\\sms\\` (see `ROMs\\README.txt`), start `Launcher.exe`. No games "
          "or ROMs are included; each game is built on your PC from your ROM. See `README.txt`.")


def app_version():
    t = open(os.path.join(ROOT, "app_info.toml"), encoding="utf-8").read()
    return re.search(r'^version\s*=\s*"([^"]+)"', t, re.M).group(1)


def section(version):
    lines, out, inside = open(os.path.join(ROOT, "CHANGELOG.md"), encoding="utf-8").read().splitlines(), [], False
    head = re.compile(r"^## v%s(?![\w.])" % re.escape(version))
    for l in lines:
        if l.startswith("## "):
            if inside:
                break
            inside = bool(head.match(l))
            continue
        if inside:
            out.append(l)
    return out


def unwrap(lines):
    out, code = [], False
    starts_block = re.compile(r"^\s*([-*+] |\d+[.)] |#|\||>)")
    for l in lines:
        if l.lstrip().startswith("```"):
            code = not code
            out.append(l)
            continue
        if code or not l.strip() or starts_block.match(l) or not out or not out[-1].strip() \
                or out[-1].lstrip().startswith(("#", "|", "```")):
            out.append(l.rstrip())
        else:                                   # a wrapped line: continues the paragraph / list item above
            out[-1] = out[-1].rstrip() + " " + l.strip()
    return out


def main():
    args = sys.argv[1:]
    version = args[0] if args else app_version()
    body = unwrap(section(version))
    if not "".join(body).strip():
        sys.exit("CHANGELOG.md has no '## v%s' section" % version)
    text = "\n".join(body).strip() + "\n\n---\n" + FOOTER + "\n"
    if len(args) > 1:
        open(args[1], "w", encoding="utf-8", newline="\n").write(text)
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stdout.write(text)


main()
