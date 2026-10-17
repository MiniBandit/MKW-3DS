FROM python:3.14-slim-trixie

# install minimal prerequisites
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       curl \
       git \
       wget \
    && rm -rf /var/lib/apt/lists/*

# install nodejs 24 LTS (nodesource)
RUN curl -fsSL https://deb.nodesource.com/setup_24.x \
       -o /tmp/nodesource.sh \
    && bash /tmp/nodesource.sh \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/* /tmp/nodesource.sh

# install devkitpro with 3ds tools
RUN wget -U "dkp-apt" https://apt.devkitpro.org/install-devkitpro-pacman \
    && chmod +x ./install-devkitpro-pacman \
    && sed -i \
       -e 's/apt-get install apt-transport-https/apt-get install -y apt-transport-https/' \
       -e 's/apt-get install devkitpro-pacman/apt-get install -y devkitpro-pacman/' \
       ./install-devkitpro-pacman \
    && ./install-devkitpro-pacman \
    && ln -s /proc/self/mounts /etc/mtab \
    && rm ./install-devkitpro-pacman \
    && dkp-pacman -Sy --noconfirm \
    && dkp-pacman -S --needed --noconfirm 3ds-dev \
    && rm -rf /var/cache/pacman/pkg/*

CMD ["bash", "./build-mkw-cia-docker.sh"]
