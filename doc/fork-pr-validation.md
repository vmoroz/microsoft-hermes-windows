# Running Hermes PR validation workflows on a fork

This guide configures a fork (for example, `vmoroz/microsoft-hermes-windows`) to run the same PR workflows used by `facebook/hermes` for `static_h` PRs.

## 1) Enable GitHub Actions in your fork

In your fork repository settings:

- `Settings` -> `Actions` -> `General`
- Set `Actions permissions` to **Allow all actions and reusable workflows**
- Under `Workflow permissions`, select **Read and write permissions**
- Save

## 2) Enable fork validation jobs in `rn-build-hermes`

In your fork repository settings:

- `Settings` -> `Secrets and variables` -> `Actions` -> `Variables`
- Add repository variable:
  - Name: `HERMES_ENABLE_FORK_VALIDATION`
  - Value: `true`

This allows the `set_release_type` job in `.github/workflows/rn-build-hermes.yml` to run on your fork. Without it, dependent jobs are skipped.

## 3) (Optional) Add release-related secrets

Most PR validation should run without release credentials when using `dry-run`. If you also want release-like packaging/publishing paths to fully execute, add these repository secrets:

- `ORG_GRADLE_PROJECT_SIGNING_PWD`
- `ORG_GRADLE_PROJECT_SIGNING_KEY`
- `ORG_GRADLE_PROJECT_SONATYPE_USERNAME`
- `ORG_GRADLE_PROJECT_SONATYPE_PASSWORD`
- `GHA_NPM_TOKEN`

If these are missing, release/publish steps may fail depending on execution path.

## 4) Open PRs against your fork `static_h`

Create PRs in your fork with base branch `static_h`.

Workflows with `pull_request` on `static_h` should trigger automatically:

- `.github/workflows/build.yml`
- `.github/workflows/rn-build-hermes.yml`

## 5) Manual fallback trigger

You can also run `RN Build Static Hermes` manually from the Actions tab using `workflow_dispatch`.
Use `release-type = dry-run` for validation.

## Notes

- This setup keeps upstream `facebook/hermes` behavior unchanged.
- ARM runner availability can vary by account/plan; if an ARM job cannot be scheduled in your fork, GitHub will show a runner availability error.
