# Contributing to XPort

Thanks for your interest in contributing! XPort is a free, community project and all help is welcome.

## Workflow

1. Fork the repo and create a feature or fix branch off `master`.
2. Keep changes focused: one feature or fix per pull request. For larger changes, open an issue first so the approach
   can be discussed.
3. Open a pull request targeting `master`.
4. CI (`.github/workflows/build-xbox.yml`) builds `lib/` and runs its unit tests on Windows with clang-cl, and builds
   and packages the Xbox app. The package is attached to the run as the `XPort-xbox-x64` artifact.

## Where code goes

- Protocol, streaming and feature logic belongs in `lib/` (portable C), not in the Xbox app. The app only maps
  inputs and outputs: controllers, video, audio, storage and UI.
- `lib/` is shared with upstream [chiaki-ng](https://github.com/streetpea/chiaki-ng). Keep changes there small and
  guarded (`#ifdef _MSC_VER`, `#if WINAPI_FAMILY...`) so upstream fixes stay easy to cherry-pick; prefer new files
  over sweeping rewrites.
- See [ROADMAP.md](ROADMAP.md) for what's planned next.

## Testing

- `lib/` changes: build and run `chiaki-unit` (see the README), ideally on both Linux and Windows/clang-cl.
- Xbox app changes: compiling is not verification. Install the package on an Xbox in Dev Mode and check the app's
  `xport.log` (see [xbox/README.md](xbox/README.md)).

AI-assisted contributions are welcome. Follow good coding standards and fully test your changes before submitting.
