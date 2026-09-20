# Publishing to PyPI

`gsusb-canfd` is published from GitHub Actions with
[Trusted Publishing](https://docs.pypi.org/trusted-publishers/) (OIDC), so no API
token or secret is stored. The workflow is `.github/workflows/publish.yml`.

Current status: **0.1.2 is live on PyPI** and TestPyPI (see the CHANGELOG).

## One-time setup

1. Create the project on PyPI if it does not exist yet: publish a first release
   manually or configure a *pending publisher*.
2. On PyPI (and TestPyPI), add a **pending publisher** for the project
   `gsusb-canfd`:
   - Owner: `sorrowfeng`
   - Repository: `gsusb-canfd`
   - Workflow: `publish.yml`
   - Environment: `pypi` (and a second one with `testpypi`)
3. In the GitHub repository, create two environments named `pypi` and `testpypi`
   (Settings → Environments). They need no secrets.

## Release checklist

1. Bump the version in `python/src/gsusb_canfd/_version.py` (single source of
   truth; `pyproject.toml` reads it dynamically and CMake generates
   `canfd/version.hpp` for the C++/C ABI from it).
2. Commit and push.
3. Publish to TestPyPI first (manual): **Actions → publish → Run workflow**, choose
   `testpypi`. Then verify:

   ```bash
   python -m pip install --index-url https://test.pypi.org/simple/ \
       --extra-index-url https://pypi.org/simple/ gsusb-canfd
   python -c "import gsusb_canfd; print(gsusb_canfd.__version__)"
   ```

4. Publish to PyPI by pushing a tag (use the version you just bumped, e.g. `v0.1.2`):

   ```bash
   git tag v0.1.2
   git push origin v0.1.2
   ```

   (Or run the workflow manually with `target: pypi`.)

## Local dry run (optional)

```bash
python -m pip install --upgrade build twine
python -m build ./python          # -> python/dist/*.whl, *.tar.gz
python -m twine check python/dist/*
python -m pip install ./python/dist/*.whl
python -c "import gsusb_canfd; print(gsusb_canfd.__version__)"
```

The wheel is pure Python (`py3-none-any`); the only runtime dependency is
`pyusb`, which in turn needs the system `libusb-1.0`.
