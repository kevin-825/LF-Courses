# syntax=docker/dockerfile:1
FROM ubuntu:26.04

LABEL maintainer="kevin@riscv.dev"
ENV DEBIAN_FRONTEND=noninteractive

ARG UID=1000
ARG GID=1000
ARG USERNAME="kevin"
ENV HOME=/home/$USERNAME
ARG WGET_ARGS="-q --show-progress --progress=bar:force:noscroll"
# ==========================================
# 1. Complete LFD401 Course Packages (Chapters 1-35)
# ==========================================
RUN <<'EOF'
#!/bin/bash
set -euo pipefail

echo ">>> [Bootstrap] Enabling full manpages & installing complete LFD401 suite..."
yes | unminimize || true

PACKAGES=(
    # --- System & Locale Utilities ---
    locales
    lsb-release
    ca-certificates
    sudo
    curl
    wget
    vim
    nano
    tmux
    tree
    less
    jq
    file
    xxd
    bsdextrautils
    dos2unix
    rsync
    openssh-client
    cpio
    lz4
    zstd
    python3

    # --- Ch 03 & 07: Source Control & OSS Patch Workflows (Git, SVN, CVS, RCS) ---
    git
    subversion
    cvs
    rcs
    patch
    diffutils
    diffstat

    # --- Ch 04, 05 & 06: Compilers, Toolchains, Libraries & Build Systems ---
    build-essential
    gcc
    g++
    clang
    lld
    llvm
    libclang-rt-dev
    make
    cmake
    ninja-build
    meson
    autoconf
    automake
    libtool
    libtool-bin
    pkg-config
    ccache
    indent
    universal-ctags
    cscope
    binutils
    elfutils
    patchelf
    chrpath

    # --- Ch 08 & 09: Debugging, Core Dumps, Sanitizers, Profiling & Tracing ---
    gdb
    gdbserver
    valgrind
    strace
    ltrace
    electric-fence
    cppcheck
    clang-tidy
    clang-format
    gcovr
    lcov
    linux-tools-common
    linux-tools-generic
    bpftrace
    time

    # --- Ch 10-22: System Calls, Memory, Filesystems (ext2/3/4), I/O, Processes, AIO, Signals & Pthreads ---
    libc6-dev
    libc6-dbg
    linux-libc-dev
    libpthread-stubs0-dev
    e2fsprogs
    inotify-tools
    util-linux
    procps
    psmisc
    lsof
    htop
    sysstat
    libaio-dev
    liburing-dev
    libnuma-dev
    numactl
    libcap-dev
    libcap2-bin
    libseccomp-dev
    libacl1-dev
    acl
    libattr1-dev
    attr
    libelf-dev
    libdw-dev
    libncurses-dev
    libreadline-dev
    libssl-dev
    zlib1g-dev

    # --- Ch 23-35: Networking, Raw/Unix/Internet Sockets, Netlink & POSIX/SysV IPC ---
    iproute2
    iputils-ping
    net-tools
    netcat-openbsd
    socat
    tcpdump
    bind9-dnsutils
    inetutils-traceroute
    ethtool
    libmnl-dev
    libnl-3-dev
    libnl-genl-3-dev

    # --- Packaging & Distribution Tools ---
    dpkg-dev
    fakeroot
    lintian
    rpm

    # --- Comprehensive Man Pages (Sections 2, 3, 7 & POSIX) ---
    man-db
    manpages
    manpages-dev
    manpages-posix
    manpages-posix-dev

    # --- Shell & Fonts ---
    zsh
    fzf
    fonts-powerline
    fonts-noto-cjk
)

apt-get update
apt-get install -y --no-install-recommends "${PACKAGES[@]}"
apt-get autoremove --purge -y
apt-get clean -y
rm -rf /var/lib/apt/lists/*
EOF

# ==========================================
# 2. Initialize System Locale
# ==========================================
RUN locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8
ENV LANGUAGE=en_US:en
ENV LC_ALL=en_US.UTF-8

# ==========================================
# 3. User Creation & Shell Setup
# ==========================================
WORKDIR /home/${USERNAME}
ENV ZSH="/home/${USERNAME}/.oh-my-zsh"

RUN --mount=type=bind,target=/build_context,rw <<'EOF'
#!/bin/bash
set -euo pipefail

if id "ubuntu" &>/dev/null; then
    echo ">>> Removing default 'ubuntu' user to free up UID 1000"
    userdel -r ubuntu || true
fi

echo ">>> Creating user '${USERNAME}' with UID ${UID} and GID ${GID}"
groupadd -g "${GID}" -o "${USERNAME}"
useradd -u "${UID}" -m -g "${USERNAME}" -G sudo,plugdev -s /usr/bin/zsh "${USERNAME}"

echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > "/etc/sudoers.d/${USERNAME}"
chmod 0440 "/etc/sudoers.d/${USERNAME}"

# Install Oh-My-Zsh
if [ -f /build_context/install_omz.sh ]; then
    echo ">>> Using local Oh-My-Zsh installer..."
    sh /build_context/install_omz.sh --unattended
else
    curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh -o /tmp/install_omz.sh
    sh /tmp/install_omz.sh --unattended
    rm /tmp/install_omz.sh
fi

ZSH_CUSTOM="$ZSH/custom"

# Install Plugins & Themes
git clone https://github.com/zsh-users/zsh-autosuggestions "${ZSH_CUSTOM}/plugins/zsh-autosuggestions"
git clone https://github.com/zsh-users/zsh-syntax-highlighting "${ZSH_CUSTOM}/plugins/zsh-syntax-highlighting"
git clone https://github.com/zsh-users/zsh-history-substring-search "${ZSH_CUSTOM}/plugins/zsh-history-substring-search"
git clone --depth=1 https://github.com/romkatv/powerlevel10k.git "${ZSH_CUSTOM}/themes/powerlevel10k"
git clone --depth=1 https://github.com/sindresorhus/pure.git "${ZSH_CUSTOM}/themes/pure"

# Pre-configure helpful GDB defaults for LFD401
printf "set pagination off\nset print pretty on\n" > "/home/${USERNAME}/.gdbinit"

# Fix ownership for the entire home directory
chown -R "${USERNAME}:${GID}" "/home/${USERNAME}"
EOF

# ==========================================
# 4. Final Entrypoint & User Config
# ==========================================
COPY --chown=${USERNAME}:${GID} ./.zshrc ${HOME}/.zshrc
COPY --chown=${USERNAME}:${GID} ./entrypoint.sh /home/entrypoint.sh

RUN chmod +x /home/entrypoint.sh

USER $USERNAME

ENV MyENV=devcontainer
VOLUME ["/workdir"]

ENTRYPOINT ["/home/entrypoint.sh"]
CMD ["zsh"]