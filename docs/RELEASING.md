# Releasing CK3 DNA Manager Qt

The release workflow is
[`.github/workflows/release.yml`](../.github/workflows/release.yml). It builds
Windows and Linux packages with Qt 6.8.3 and creates a GitHub Release when a tag
matching `v*` is pushed.

## What a release contains

- `CK3-DNA-Manager-<version>-win64.zip`
- `CK3-DNA-Manager-<version>-linux-x86_64.AppImage`
- `CK3-DNA-Manager-<version>-linux-x86_64.tar.gz`
- `SHA256SUMS.txt`

The release body is generated from the commit subjects since the previous tag.
You can edit the release notes afterwards from the GitHub Releases page.

Packaged builds include a short `README.md` copied from
[`docs/PACKAGE_README.md`](PACKAGE_README.md), not the full repository README.

## Publish a release

Review the changes, commit them, push `main`, then push the version tag:

```bash
git status
git add .
git commit -m "Release 1.0.1"
git push origin main
git tag v1.0.1
git push origin v1.0.1
```

The tag push starts the release build. When it finishes, the release is at
<https://github.com/grnnk/CK3-DNA-Manager-Qt/releases/tag/v1.0.1>.

Do not create the GitHub Release manually first; let the workflow create it.
If the release already exists, the workflow updates it with the generated notes
and assets.

## Test the builds without publishing

1. Open the repository on GitHub.
2. Click **Actions**.
3. Select **Build and publish release**.
4. Click **Run workflow**, choose `main`, and confirm.

The Windows and Linux jobs build and upload artifacts. The publishing job is
skipped because it only runs for tags. Download `windows-release` and
`linux-release` from the run summary.

The workflow can also be dispatched on an existing tag if the tag run needs to
be restarted:

```bash
gh workflow run release.yml --ref v1.0.1
```

Pushing the tag already starts the workflow, so this is normally only needed
after a failed run. The **Re-run all jobs** button on the failed run works too.

## GitHub Pages

The Pages workflow is
[`.github/workflows/pages.yml`](../.github/workflows/pages.yml). It deploys the
`web/` folder on every push to `main`.

Before the first deployment, open **Settings → Pages** and set **Source** to
**GitHub Actions**. The site is published at
<https://grnnk.github.io/CK3-DNA-Manager-Qt/>.

The workflow can also be run manually from **Actions → Deploy web app to GitHub
Pages → Run workflow**.
