---
title: Writing a blog post
summary: Posts are markdown files in the site branch. Push, and the site rebuilds itself.
---
The site source lives in the **`site`** branch of the DaySetta repo. Every push to that branch runs a GitHub Action (`.github/workflows/pages.yml`) that rebuilds the site and publishes it to GitHub Pages. You can also run it by hand from the Actions tab ("build site" → Run workflow). The devlog page is built from the copy of `devlog/decisions.md` kept in `content/devlog/`.

## Add a post

1. Open the `site` branch on GitHub and go to `content/blog/`.
2. Create a file named `YYYY-MM-DD-short-slug.md`, for example `2026-11-30-first-light.md`.
3. Start it with a front matter block, then write markdown:

```
---
title: First light
summary: One sentence that shows on the blog index.
tags: gpu, milestone
---
Your post in **markdown**. Code blocks, tables, links and images work.
```

4. Commit. About a minute later the post is live at `/blog/short-slug/` and in the RSS feed.

## Images

Put images in `content/blog/img/` and link them as `![alt text](img/name.png)`. Keep them small (webp, under ~300 KB).

## House rules

- Truth only: every claim should be backed by the devlog, a log line or a commit.
- No fake screenshots of a macOS desktop. When there is a real one, it will be the best post on this blog.
- No Apple logos or artwork.
