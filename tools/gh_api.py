#!/usr/bin/env python3
"""gh_api.py - GitHub-operaatiot yoajolle ILMAN gh-CLI:ta.

Miksi tama on olemassa: yoajo on ajastettu pilviajo, ja siina ymparistossa
`gh` EI ole asennettu (todettu 13.9.2026). Sen sijaan GH_TOKEN/GITHUB_TOKEN
ovat ymparistossa valmiina, joten REST-API on suoraan kaytettavissa.

Tulosteet ovat tarkoituksella tiiviita yhden rivin muotoja: ne paatyvat
agentin kontekstiin, ja raaka JSON maksaisi tuhansia tokeneita per kutsu.
Vain stdlib - ei riippuvuuksia, koska tama ajetaan paljaassa kontissa.
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request

DEFAULT_REPO = "SpaceRodento/Suckboxx"
API_ROOT = "https://api.github.com"


def resolve_token():
    for name in ("GH_TOKEN", "GITHUB_TOKEN"):
        token = os.environ.get(name)
        if token:
            return token
    sys.stderr.write("gh_api: GH_TOKEN/GITHUB_TOKEN puuttuu ymparistosta\n")
    sys.exit(2)


def resolve_repo(args_repo):
    return args_repo or os.environ.get("SBX_GH_REPO") or DEFAULT_REPO


def _ssl_context():
    """SSL_CERT_FILE jos asetettu, sitten certifi jos asennettu, muuten oletus.

    Windowsin Python lukee juurivarmenteet Windowsin varmennesailosta, josta
    GitHubin nykyinen juuri voi puuttua (Windows hakee juuret vasta tarvittaessa,
    Python ei osaa pyytaa): 'unable to get issuer certificate', todettu
    30.10.2026. Pilvikontin valityspalvelin allekirjoittaa yhteydet omalla
    juurellaan, jonka vain SSL_CERT_FILE tuntee. Kun pytestin riippuvuudet
    toivat certifin, se ohitti juuren ja kaatoi jokaisen kutsun (koeajo R
    30.10.2026).
    """
    if os.environ.get("SSL_CERT_FILE"):
        return None
    try:
        import certifi
    except ImportError:
        return None
    import ssl
    return ssl.create_default_context(cafile=certifi.where())


def api(path, repo, method="GET", payload=None, params=None):
    """Yksi REST-kutsu. Palauttaa (status, parsed_json_or_None)."""
    url = "%s/repos/%s/%s" % (API_ROOT, repo, path.lstrip("/"))
    if params:
        url += "?" + urllib.parse.urlencode(params)
    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Authorization", "Bearer " + resolve_token())
    req.add_header("Accept", "application/vnd.github+json")
    req.add_header("User-Agent", "suckboxx-gh-api")
    if data:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=30, context=_ssl_context()) as resp:
            body = resp.read().decode("utf-8")
            return resp.status, (json.loads(body) if body.strip() else None)
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", "replace")
        try:
            parsed = json.loads(body)
        except ValueError:
            parsed = {"message": body[:200]}
        return exc.code, parsed
    except (urllib.error.URLError, OSError, ValueError) as exc:
        sys.stderr.write("gh_api: verkkovirhe: %s\n" % exc)
        sys.exit(3)


def fail(status, parsed):
    msg = ""
    if isinstance(parsed, dict):
        msg = parsed.get("message", "")
    sys.stderr.write("gh_api: HTTP %s %s\n" % (status, msg))
    sys.exit(1)


# ── luku ──────────────────────────────────────────────────────────────

def list_prs(repo, base=None, state="open"):
    params = {"state": state, "per_page": "50"}
    if base:
        params["base"] = base
    status, data = api("pulls", repo, params=params)
    if status != 200:
        fail(status, data)
    return data


def pr_files(repo, number):
    names = []
    page = 1
    while True:
        status, data = api("pulls/%d/files" % number, repo,
                           params={"per_page": "100", "page": str(page)})
        if status != 200:
            fail(status, data)
        names.extend(f["filename"] for f in data)
        if len(data) < 100:
            return names
        page += 1


def pr_comments(repo, number):
    status, data = api("issues/%d/comments" % number, repo,
                       params={"per_page": "100"})
    if status != 200:
        fail(status, data)
    return data


REVIEWED_HEAD_PREFIX = "REVIEWED-HEAD:"


def verdict_head(comment):
    """The SHA a verdict comment was reviewed against, or None.

    Y11: cmd_comment appends a `REVIEWED-HEAD: <sha>` line to every
    REVIEW-VERDICT comment it posts, so a merge can later confirm the
    reviewed commit is still the PR's head. Scanned anywhere in the body,
    not just the first line - unlike the verdict itself, its exact
    position does not need to resist spoofing by a third party's
    unrelated comment (newest_verdict_record already requires the SAME
    comment to carry both).

    Y11b: if the body carries more than one DISTINCT REVIEWED-HEAD value
    (a hand-edited comment, or two conflicting lines), that is treated the
    same as having none - an ambiguous head can never authorize a merge,
    same as a missing one."""
    found = set()
    for line in (comment.get("body") or "").splitlines():
        line = line.strip()
        if line.startswith(REVIEWED_HEAD_PREFIX):
            found.add(line.split(":", 1)[1].strip())
    if len(found) == 1:
        return next(iter(found))
    return None


def newest_verdict_record(comments):
    """(verdict, reviewed_head) from the newest REVIEW-VERDICT comment, or
    (None, None). Both come from the SAME comment - see newest_verdict's
    own docstring for why the verdict must be the first line."""
    verdict, head = None, None
    for comment in comments:
        first = (comment.get("body") or "").strip().splitlines()
        if not first:
            continue
        line = first[0].strip()
        if line.startswith("REVIEW-VERDICT:"):
            verdict = line.split(":", 1)[1].strip()
            head = verdict_head(comment)
    return verdict, head


def newest_verdict(comments):
    """Uusin REVIEW-VERDICT-rivi voittaa. None jos yhtaan ei ole.

    Tama on merkkijonovertailu, ei arvio toisen proosasta: rivin ON oltava
    kommentin ENSIMMAINEN rivi. Botin "en pystynyt katselmoimaan" ei siis
    koskaan nayta katselmukselta.
    """
    return newest_verdict_record(comments)[0]


def current_verdict(comments, current_head):
    """The newest verdict, or None once the PR head has moved past the head
    that verdict recorded - new commits (a fix after defect-found, a dev
    merge) need a fresh review. On 26.10.2026 #333 got its fix after a
    defect-found verdict, and no night run would have reviewed it again:
    step 3b only picks PRs whose verdict is `none`. A verdict with no
    recorded head (pre-Y11b), or an unknown current head, cannot be compared
    and keeps its value."""
    verdict, head = newest_verdict_record(comments)
    if verdict is not None and head and current_head and head != current_head:
        return None
    return verdict


def pr_info(repo, number):
    status, data = api("pulls/%d" % number, repo)
    if status != 200:
        fail(status, data)
    return data


def pr_head_sha(repo, number):
    return pr_info(repo, number)["head"]["sha"]


def rollup(repo, number, sha=None):
    """Kolmiarvoinen tulos: success / failure / pending / no-checks.

    Kolmas on TAYSIN NORMAALI tassa repossa: jokainen workflow on
    path-filteroity, joten docs- tai scripts-only-PR ei osu yhteenkaan.
    Kahden arvon luenta merkitsisi ettei sellaista voisi mergeta koskaan.

    Lahde on VAIN check-runs. Vanha combined-status-endpoint vastaa talla
    tokenilla 403 ("Resource not accessible by integration", mitattu
    13.9.2026), eika sita tarvita: taman repon kaikki portit ovat GitHub
    Actions -workflowja, ja ne nakyvat check-runsina. Jos repoon joskus
    lisataan ulkoinen status-context, se EI nay tassa - silloin tama
    funktio on paivitettava eika sen tulosta saa uskoa sellaisenaan.

    `sha` lets a caller that already resolved the head (cmd_merge) reuse
    it instead of paying for a second pulls/<n> lookup.
    """
    sha = sha or pr_head_sha(repo, number)
    status, runs = api("commits/%s/check-runs" % sha, repo,
                       params={"per_page": "100"})
    if status != 200:
        fail(status, runs)

    return classify_runs(runs.get("check_runs", []))


def classify_runs(check_runs):
    """(rivit, tulos) check-run-listasta. Erotettu jotta kolmiarvoisuus on
    testattavissa ilman verkkoa — se on koko tiedoston hienovaraisin kohta."""
    rows = []
    pending = False
    failed = False
    for run in check_runs:
        concl = run.get("conclusion") or ""
        rows.append((run.get("name", "?"), run.get("status", "?"), concl or "-"))
        if run.get("status") != "completed":
            pending = True
        elif concl not in ("success", "neutral", "skipped"):
            failed = True

    if not rows:
        return rows, "no-checks"
    if failed:
        return rows, "failure"
    if pending:
        return rows, "pending"
    return rows, "success"


def find_by_head(repo, prefix):
    """Haku identiteetilla: KAIKKI PR:t sivutettuna, ei katkaistua listaa.

    Talla on oma historiansa: `gh pr list --state all --limit 30` paattyi
    7.9.2026 aamuna PR #408:aan, kun paketti 44 oli jo mergetty #407:na —
    saantoa kirjaimellisesti noudattava ajo olisi rakentanut sen uudelleen.
    Siksi tama sivuttaa loppuun asti: tyhja tulos on oikeasti tyhja.

    search/issues olisi halvempi, mutta se vastaa talla tokenilla 403
    (mitattu 13.9.2026) — alä yrita sita uudelleen ilman etta tarkistat
    tokenin oikeudet ensin.
    """
    hits = []
    page = 1
    while True:
        status, data = api("pulls", repo, params={
            "state": "all", "per_page": "100", "page": str(page)})
        if status != 200:
            fail(status, data)
        for pr in data:
            if pr.get("head", {}).get("ref", "").startswith(prefix):
                hits.append(pr)
        if len(data) < 100:
            return hits
        page += 1


# ── komennot ──────────────────────────────────────────────────────────

def cmd_prs(args, repo):
    for pr in list_prs(repo, base=args.base, state=args.state):
        print("#%d\t%s\t%s\t%s" % (pr["number"], pr["base"]["ref"],
                                   pr["created_at"], pr["title"]))
    return 0


def cmd_files(args, repo):
    for name in pr_files(repo, args.number):
        print(name)
    return 0


def cmd_comments(args, repo):
    for comment in pr_comments(repo, args.number):
        print("--- %s %s" % (comment["user"]["login"], comment["created_at"]))
        print(comment.get("body") or "")
    return 0


def cmd_verdict(args, repo):
    comments = pr_comments(repo, args.number)
    print(current_verdict(comments, pr_head_sha(repo, args.number)) or "none")
    return 0


def cmd_checks(args, repo):
    rows, verdict = rollup(repo, args.number)
    for name, status, concl in rows:
        print("%s\t%s\t%s" % (name, status, concl))
    print("ROLLUP: %s" % verdict)
    return 0


def cmd_find(args, repo):
    for pr in find_by_head(repo, args.head):
        print("#%d\t%s\t%s\t%s" % (pr["number"], pr["state"],
                                   pr["head"]["ref"], pr["title"]))
    return 0


def cmd_merge(args, repo):
    """Merge #<number>, pinned to the exact head the newest no-defect
    verdict reviewed (Y11). This closes the gap where a push landed
    between the review and this call: without it, the merge would take
    whatever the head happens to be right now, unreviewed commits
    included.

    Three refusals before any merge call is attempted, each left for a
    human/night re-review rather than retried automatically:
      - no recorded head on the newest no-defect verdict (a pre-Y11
        comment, or REVIEWED-HEAD stripped) - not authorizing a merge;
      - the reviewed head is not the PR's current head - a commit landed
        since the review;
      - checks are failure/pending, or no-checks on a PR not based on
        `dev` (where CI by design never runs - RUTIINI.md).
    GitHub's own `sha` parameter on the merge endpoint is the last line
    of defense against a push racing this very call.
    """
    pr = pr_info(repo, args.number)
    current_head = pr["head"]["sha"]
    base_ref = pr["base"]["ref"]

    verdict, reviewed_head = newest_verdict_record(pr_comments(repo, args.number))
    if verdict != "no-defect":
        sys.stderr.write("gh_api: newest verdict is %r, not authorizing a merge\n" % verdict)
        return 1
    if reviewed_head is None:
        sys.stderr.write(
            "gh_api: the no-defect verdict has no recorded head (pre-Y11 comment, "
            "or it was edited) - re-review required\n")
        return 1
    if reviewed_head != current_head:
        sys.stderr.write(
            "gh_api: PR head moved since review (%s -> %s) - re-review required\n"
            % (reviewed_head, current_head))
        return 1

    _rows, check_verdict = rollup(repo, args.number, sha=current_head)
    if check_verdict in ("failure", "pending"):
        sys.stderr.write("gh_api: checks are %s - not merging\n" % check_verdict)
        return 1
    if check_verdict == "no-checks" and base_ref != "dev/integraatio":
        sys.stderr.write(
            "gh_api: no checks ran on a %r-based PR (only dev-based PRs skip CI "
            "by design) - refusing\n" % base_ref)
        return 1

    status, data = api("pulls/%d/merge" % args.number, repo, method="PUT",
                       payload={"merge_method": args.method, "sha": reviewed_head})
    if status not in (200, 201):
        if status in (405, 409):
            sys.stderr.write(
                "gh_api: GitHub rejected the merge (head mismatch) - re-review required\n")
            return 1
        fail(status, data)
    print("merged #%d" % args.number)
    return 0


def cmd_create(args, repo):
    body = open(args.body_file, encoding="utf-8").read()
    status, data = api("pulls", repo, method="POST", payload={
        "title": args.title, "head": args.head, "base": args.base,
        "body": body, "draft": False})
    if status not in (200, 201):
        fail(status, data)
    print("#%d %s" % (data["number"], data["html_url"]))
    return 0


def cmd_comment(args, repo):
    """Post one comment. When its first line is a REVIEW-VERDICT (any of
    the three values - cmd_merge is what cares which one), append a
    REVIEWED-HEAD line (Y11) so a later merge can confirm nothing landed
    since this review. An ordinary comment costs no extra API call and is
    never touched.

    Y11b: the SHA recorded is `--reviewed-head`, the commit the CALLER
    captured before reading the diff (e.g. from the `git fetch origin
    <head>` it already does to review) - never a SHA this function fetches
    itself at post time. Y11 fetched the PR's head here, at posting time,
    which is AFTER the review; a push landing during the review (between
    the caller capturing the head and this call) was then recorded as
    reviewed even though the diff actually read was the older commit - the
    bug this packet (Y11b) exists to close.

    `--reviewed-head` is required for a REVIEW-VERDICT comment (missing it
    refuses before any API call: a verdict with no claimed head can never
    authorize a merge, so it must never be silently invented here either).
    The live PR head is then fetched and compared: if it still matches
    `--reviewed-head`, that is proof nothing landed mid-review and the
    comment posts normally; if it does not, a push raced the review and
    this refuses before POST, so the stale verdict is never recorded as
    covering code it did not see - re-review the new head instead."""
    body = open(args.body_file, encoding="utf-8").read()
    first_line = body.strip().splitlines()[0].strip() if body.strip() else ""
    if first_line.startswith("REVIEW-VERDICT:"):
        if REVIEWED_HEAD_PREFIX in body:
            sys.stderr.write(
                "gh_api: body already contains a %s line - this is appended here, "
                "the caller must not write it\n" % REVIEWED_HEAD_PREFIX)
            return 1
        if not args.reviewed_head:
            sys.stderr.write(
                "gh_api: --reviewed-head <sha> is required for a REVIEW-VERDICT "
                "comment - capture it before reading the diff (the commit you "
                "`git fetch`ed to review), not at posting time\n")
            return 1
        current_head = pr_head_sha(repo, args.number)
        if current_head != args.reviewed_head:
            sys.stderr.write(
                "gh_api: PR head moved since the review started (%s -> %s) - "
                "a push landed mid-review; re-review the new head instead of "
                "posting this verdict\n" % (args.reviewed_head, current_head))
            return 1
        body = body.rstrip("\n") + "\n\n%s %s\n" % (REVIEWED_HEAD_PREFIX, current_head)
    status, data = api("issues/%d/comments" % args.number, repo,
                       method="POST", payload={"body": body})
    if status not in (200, 201):
        fail(status, data)
    print("commented #%d" % args.number)
    return 0


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", help="owner/name (oletus %s)" % DEFAULT_REPO)
    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("prs"); p.add_argument("--base"); p.add_argument("--state", default="open"); p.set_defaults(fn=cmd_prs)
    p = sub.add_parser("files"); p.add_argument("number", type=int); p.set_defaults(fn=cmd_files)
    p = sub.add_parser("comments"); p.add_argument("number", type=int); p.set_defaults(fn=cmd_comments)
    p = sub.add_parser("verdict"); p.add_argument("number", type=int); p.set_defaults(fn=cmd_verdict)
    p = sub.add_parser("checks"); p.add_argument("number", type=int); p.set_defaults(fn=cmd_checks)
    p = sub.add_parser("find"); p.add_argument("--head", required=True); p.set_defaults(fn=cmd_find)
    p = sub.add_parser("merge"); p.add_argument("number", type=int); p.add_argument("--method", default="merge"); p.set_defaults(fn=cmd_merge)
    p = sub.add_parser("create"); p.add_argument("--base", required=True); p.add_argument("--head", required=True); p.add_argument("--title", required=True); p.add_argument("--body-file", required=True); p.set_defaults(fn=cmd_create)
    p = sub.add_parser("comment"); p.add_argument("number", type=int); p.add_argument("--body-file", required=True); p.add_argument("--reviewed-head", help="SHA captured before reading the diff; required when the body's first line is REVIEW-VERDICT:"); p.set_defaults(fn=cmd_comment)
    return parser


def main():
    # PR titles carry Finnish text and dashes; the Windows console (cp1252)
    # would abort mid-print. Kopioitu Zignalmeisterin scripts/gh_api.py:stä 31.10.2026.
    for _stream in (sys.stdout, sys.stderr):
        if hasattr(_stream, "reconfigure"):
            _stream.reconfigure(encoding="utf-8", errors="replace")
    args = build_parser().parse_args()
    return args.fn(args, resolve_repo(args.repo))


if __name__ == "__main__":
    sys.exit(main())
