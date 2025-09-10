# Contributing to VrootKV

First off, thank you for considering contributing to VrootKV! It's people like you that make open source such a great community.

## Code of Conduct

This project and everyone participating in it is governed by the [VrootKV Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code. Please report unacceptable behavior to [vrutikhalani6@gmail.com](mailto:vrutikhalani6@gmail.com).

## How Can I Contribute?

### Reporting Bugs

This is one of the easiest and most helpful ways to contribute. If you find a bug, please open an issue and provide the following information:

*   A clear and descriptive title.
*   A detailed description of the problem, including steps to reproduce it.
*   The expected behavior and what you observed instead.
*   Your operating system and compiler versions.

### Suggesting Enhancements

If you have an idea for a new feature or an improvement to an existing one, please open an issue and provide the following information:

*   A clear and descriptive title.
*   A detailed description of the proposed enhancement.
*   The motivation for the enhancement.

### Pull Requests

We welcome pull requests! If you want to contribute code, please follow these steps:

1.  Fork the repository and create a new branch from `main`.
2.  Make your changes and make sure to add or update tests as appropriate.
3.  Make sure the tests pass by running `ctest` in the `build` directory.
4.  Make sure your code follows the existing coding style.
5.  Open a pull request with a clear and descriptive title and a detailed description of your changes.

## Styleguides

### Git Commit Messages

*   Use the present tense ("Add feature" not "Added feature").
*   Use the imperative mood ("Move cursor to..." not "Moves cursor to...").
*   Limit the first line to 72 characters or less.
*   Reference issues and pull requests liberally after the first line.

### C++ Styleguide

*   Follow the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html).
*   Use `clang-format` to format your code.

## Setting up your repository for contributions

To ensure that all contributions follow the project's coding standards, we use a pre-commit hook to run `clang-format` and check for formatting errors. To enable this hook, run the following command from the root of the repository:

```bash
ln -s ../../pre-commit.sh .git/hooks/pre-commit
```

This will create a symbolic link to the `pre-commit.sh` script in the `.git/hooks` directory. Now, every time you run `git commit`, the pre-commit hook will be executed.
