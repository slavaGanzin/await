# await — Promotion Ideas

Compiled by automated research (runs Tue/Thu 10:17am). Never remove entries — only add or update status.

---

## GitHub Awesome Lists
Permanent inbound traffic, SEO juice, takes ~10 min per PR.

- [ ] [agarrharr/awesome-cli-apps](https://github.com/agarrharr/awesome-cli-apps) (19.5k★) — add under Utilities → Shell Utilities. Requires 90+ days old repo and 20+ stars. Entry: `[await](https://github.com/slavaGanzin/await) - Poll shell commands until they succeed, replacing fragile sleep loops.` | **Effort: low**
- [ ] [alebcay/awesome-shell](https://github.com/alebcay/awesome-shell) (36.9k★) — add under System Utilities | **Effort: low**
- [ ] [awesome-lists/awesome-bash](https://github.com/awesome-lists/awesome-bash) — add under Utilities | **Effort: low**
- [ ] [awesome-foss/awesome-sysadmin](https://github.com/awesome-foss/awesome-sysadmin) (33.8k★) — add under Automation | **Effort: low**

---

## Communities / Forums

- [ ] **r/commandline** (~100k) — "I got tired of writing sleep loops so I made a 36KB C binary that polls commands until they succeed". Before/after code snippet. | **Effort: low**
- [ ] **r/bash** (~200k) — "Replace your sleep/retry loops with one command — await polls until exit 0" | **Effort: low**
- [ ] **r/devops** (~250k) — focus on CI/CD angle: wait for DB/Redis/port before running migrations | **Effort: low**
- [ ] **r/sysadmin** (~900k) — "Wrote a tool for the classic sysadmin problem: wait until X is up — 36KB, no deps" | **Effort: low**
- [ ] **r/coolgithubprojects** — just post GitHub URL + one-liner | **Effort: low**
- [ ] **r/programming** (~6M) — only after traction elsewhere, frame as interesting C project | **Effort: low**
- [ ] **Lobste.rs** (~50k, high quality) — tags: cli, unix, shell, tools. Needs invite. Go deep on C implementation + exit code semantics | **Effort: med**

---

## Launch Platforms

- [ ] **Show HN** — highest leverage single action. Title: `Show HN: await – poll shell commands until they succeed, replacing sleep loops (36KB C binary)`. Post 8–10am PT Tue–Thu. Respond to every comment within minutes. **Wait until 50+ stars.** | **Effort: low**
- [ ] **Product Hunt** — needs 50+ stars, demo video, hunter with 500+ followers, network for day-1 comments. Tagline: `Replace sleep loops — poll any shell command until it succeeds`. Launch Tue/Wed 12:01am PT | **Effort: high**
- [ ] **DevHunt.org** — PR-based, lower competition than PH, good warmup. Submit at github.com/MarsX-dev/devhunt | **Effort: low**

---

## Content / Articles

- [ ] **dev.to article** — "Stop Writing Sleep Loops: Use a CLI Poller Instead". Structure: pain → failure modes → await → 4 real use cases (CI healthcheck, Docker readiness, AI agent, SSH after EC2 boot) → install. Tag: cli, devops, bash, productivity. Stays discoverable via Google for years. | **Effort: med**
- [ ] **Hashnode** — cross-post the dev.to article | **Effort: low**

---

## Newsletters

- [ ] **Changelog News** — free editorial submission at changelog.com/news/submit. 1-para pitch emphasizing the AI agent angle | **Effort: low**
- [ ] **TLDR DevOps** (350k subs, 42% open rate) — sponsor-only, ~$500–1000 per placement. Do after traction | **Effort: high**

---

## Social

- [ ] **Twitter/X** — post format: before (`sleep 30 && curl...`) vs after (`await curl...`), include GIF demo, install one-liner. Tags: #cli #devops #bash #AIagents. Post star milestones (100, 250, 500★) | **Effort: low, ongoing**

---

## AI Agent Communities
await is a perfect fit for AI agent workflows — this angle is underexplored.

- [ ] Research and list AI agent / LLM developer communities (Discord servers, GitHub Discussions on popular agent frameworks like LangChain, CrewAI, AutoGen) that would find await useful for waiting on async ops | **Effort: med**

---

## Status
- GitHub stars at time of last update: ~unknown (check: `curl -s https://api.github.com/repos/slavaGanzin/await | jq .stargazers_count`)
- Show HN threshold: 50 stars
- Product Hunt threshold: 50 stars + video + network
