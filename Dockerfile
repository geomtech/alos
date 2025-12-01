FROM randomdude/gcc-cross-i686-elf

# On installe make et nasm
RUN apt-get update && apt-get install -y make nasm qemu-system-x86

# On prépare le dossier de travail
WORKDIR /root/env
VOLUME /root/env