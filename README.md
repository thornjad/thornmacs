# aero

### _Emacs, Rust, Spacemacs and more_

![](https://img.shields.io/gitlab/pipeline/thornjad/aero.svg?style=for-the-badge) ![](https://img.shields.io/github/tag-date/thornjad/aero.svg?colorB=orange&label=version&style=for-the-badge) ![](https://img.shields.io/github/repo-size/thornjad/aero.svg?colorB=green&style=for-the-badge) ![](https://img.shields.io/github/languages/top/thornjad/aero.svg?style=for-the-badge) ![](https://img.shields.io/github/contributors/thornjad/aero.svg?style=for-the-badge) ![](https://img.shields.io/github/license/thornjad/aero.svg?style=for-the-badge)

[![Built with Spacemacs](https://cdn.rawgit.com/syl20bnr/spacemacs/442d025779da2f62fc86c2082703697714db6514/assets/spacemacs-badge.svg)](http://spacemacs.org)

<!-- markdown-toc start - Don't edit this section. Run M-x markdown-toc-refresh-toc -->
**Table of Contents**

- [What aero Is](#what-aero-is)
- [What aero Is Not](#what-aero-is-not)
- [Difference With Remacs+Spacemacs](#difference-with-aerospacemacs)
- [Tailored Use-Cases](#tailored-use-cases)
- [Why Emacs?](#why-emacs)
- [Why Remacs?](#why-aero)
- [Why Spacemacs?](#why-spacemacs)
- [Getting Started](#getting-started)
    - [Requirements](#requirements)
    - [Dockerized development environment](#dockerized-development-environment)
    - [Building aero](#building-aero)
    - [Running aero](#running-aero)
- [Design Goals](#design-goals)
- [Contributing](#contributing)
- [License](#license)

<!-- markdown-toc end -->

# What aero is

aero is, simply put, a fusion of Spacemacs and Remacs, yielding a
day-to-day editor and reliable sidekick.

aero is:

- Emacs at heart
- Integrated Spacemacs
- Increasingly Rust-powered
- Heavily tailored
- Emacs-ily extensible
- Smaller

# What aero is not

aero is not, and is not intended to be, a widespread editor. I
(thornjad) make this for myself and my own use-cases. While I try to
leave it open for extensiblity via Emacs Lisp, aero cannot be
counted on to have all the bells and whistles that come with GNU
Emacs.

aero is not:

- Intended for mass use
- Supportive of things I (thornjad) don't like
- Backed by a team
- Guranteed to work

# Difference With Remacs+Spacemacs

aero is a fusion of Remacs and Spacemacs, but goes further. Lambda:

- Is smaller
- Does not support the NT kernel
- Does not use X by default (check out the `with-gui` branch)
- Does not contain many default packages which Emacs and Spacemacs
  provide

# Tailored Use-Cases

aero is tailored to the use-case of my (thornjad) life. As such, it
is designed with these uses in mind:

- TCL backend and Apache Rivet frontend
- Lisp, primarily Common Lisp (SBCL), also HCL, Orson, and of course
  Emacs lisp
- Javascript, for Node and Web, with Handlebars and other frameworks
- Coffeescript for Node
- Occasional C, C++, Make, SQL
- Heavy git usage, both Magit and CLI (via gitsh)
- Heavy SSH usage via TRAMP
- Documentation in Markdown, Org-mode and other formats
- Terminal-oriented development (Alacritty + Tmux)
- Unix-only (GNU/Linux and MacOS)

# Why Emacs?

Emacs will change how you think about programming. Emacs is entirely
introspectable, and runs in a fully mutable and extensible
environment. It also has a fantastic culture, built around great
documentation, even providing an interactive tutorial. It also comes
with a broad ecosystem, with support running back decades.

Additionally, Emacs is, essentially, a Lisp machine, and so bringing
all the power and might of Lisp with it, inherently. This makes
working with Lisp ridiculously easy.

Besides, check out the [learning
curve](https://i.stack.imgur.com/7Cu9Z.jpg).

# Why Remacs?

Remacs offers Rust as an alternative to C. Though C is arguably
timeless, Remacs may make Emacs development more accessible to a
younger generation, allowing them to eventually take the torch from
the current core and bring Emacs into the future.

Additionally, a full port to Rust may offer a speedup, in run time as
well as compile time and developer time. It may also speed along the
full development of needed features, such as fully-asynchronous
operation.

See the [Remacs project](https://github.com/Wilfred/remacs* for more
information.

# Why Spacemacs?

Spacemacs is a beautiful and thriving project that provides aero
with two major benefits:

*Key bindings*

- Emacs key bindings suck, Spacemacs bindings (inspired by Vim are
  good
- No need to constantly press modifier keys
- Space as a leader key is brilliant, simple, and ergonomical

*Extensibility*

- Emacs is highly extensible. This is good, but...
- Spacemacs makes it easier. The single `.spacemacs` configuration
  file is intuitive in a way pure Emacs doesn't quite achieve
- Spacemacs also provides sensible default packages. Emacs does as
  well, but many are antiquated and unused

# Getting Started

## Requirements

1. You will need
   [Rust installed](https://www.rust-lang.org/en-US/install.html).
   The file `rust-toolchain` indicates the version that gets installed.
   This happens automatically, so don't override the toolchain manually.

2. You will need a C compiler and toolchain. On Linux, you can do
   something like:

        apt install build-essential automake clang libclang-dev

   On macOS, you'll need Xcode.

3. Linux:

        apt install texinfo libjpeg-dev libtiff-dev \
          libgif-dev libxpm-dev libgtk-3-dev libgnutls28-dev \
          libncurses5-dev libxml2-dev libxt-dev

   macOS:

        brew install gnutls texinfo autoconf

    To use the installed version of `makeinfo` instead of the built-in
    (`/usr/bin/makeinfo`) one, you'll need to make sure
    `/usr/local/opt/texinfo/bin` is before `/usr/bin` in
    `PATH`. Mojave install libxml2 headers with: `open
    /Library/Developer/CommandLineTools/Packages/macOS_SDK_headers_for_macOS_10.14.pkg`

## Dockerized development environment

If you don't want to bother with the above setup you can use the
provided Docker environment. Make sure you have
[docker](https://www.docker.com/) 1.12+ and
[docker-compose](https://github.com/docker/compose) 1.8+ available.

To spin up the environment run

``` shell
docker-compose up -d
```

The first time you run this command, Docker will build the image. After
that any subsequent startups will happen in less than a second. If
this command fails because of needing absolute paths, make sure to set
the PWD environment variable before calling the command like so:

```shell
PWD=$(pwd) docker-compose up -d
```

The working directory with aero will be mounted under the same path
in the container so editing the files on your host machine will
automatically be reflected inside the container. To build aero use
the steps from [Building aero](#building-aero) prefixed with
`docker-compose exec aero`, this will ensure the commands are
executed inside the container.

## Building aero

```
$ ./autogen.sh
$ ./configure --enable-rust-debug
$ make
```

For a release build, don't pass `--enable-rust-debug`.

The Makefile obeys cargo's RUSTFLAGS variable and additional options
can be passed to cargo with CARGO_FLAGS.

For example:

``` bash
$ make CARGO_FLAGS="-vv" RUSTFLAGS="-Zunstable-options --cfg MARKER_DEBUG"
```

## Running aero

You can now run your shiny new aero build!

```bash
# Using -q to ignore your .emacs.d, so aero starts up quickly.
# RUST_BACKTRACE is optional, but useful if your instance crashes.
$ RUST_BACKTRACE=1 src/aero -q
```

# Design Goals

*Tailored experience*: aero is highly tailored to me
(thornjad). This is the primary purpose of this project; if you think
this is dumb, I encourage you to fork this project and build it the
way you want.

*Documentation*: Emacs and Spacemacs have a culture of great
documentation. aero should be no different.

*Power of Rust*: Through Remacs, aero should benefit from the
performance and robustness of Rust, and the ecosystem that comes with
it.

# Contributing

![](https://img.shields.io/github/contributors/thornjad/aero.svg?style=for-the-badge)

Pull requests are welcome, but keep in mind the target audience
here. Your contribution is likely to have more impact in
[Remacs](https://github.com/Wilfred/remacs) or
[Spacemacs](https://github.com/syl20bnr/spacemacs).

# License

![](https://img.shields.io/github/license/thornjad/aero.svg?style=for-the-badge)

aero is covered under a GPLv3 License, exactly the same as its
constituent software, [GNU
Emacs](https://www.gnu.org/software/emacs/),
[Remacs](https://github.com/Wilfred/remacs) and
[Spacemacs](https://github.com/syl20bnr/spacemacs). Please see the
[license file (LICENSE)](./LICENSE) and the accompanying
[etc directory](./etc/gnu/COPYING). Additionally, some software packages
shipped in this repository carry their own copyright, in which case you can
refer to that package and/or its header(s).
